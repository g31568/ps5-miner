// SPDX-License-Identifier: GPL-3.0-or-later
// MOM object-login C29 protocol over nonblocking TCP, optional SOCKS5 and verified TLS.
#define JSON_NOEXCEPTION 1
#include "third-party/nlohmann/json.hpp"
#include "pool.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <deque>
#include <string>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#if defined(__PROSPERO__)
#include <sys/sysctl.h>
#endif
#ifdef MOM_POOL_MBEDTLS
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>
#else
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif
using Json = nlohmann::json;
namespace {
constexpr size_t line_limit = 16384, output_limit = 32768;
bool secure_random(unsigned char *out, size_t size) {
#if defined(__PROSPERO__)
  // The SDK's ps5_arc4random is a timestamp PRNG; use the kernel CSPRNG without fallback.
  int mib[] = {CTL_KERN, KERN_ARND};
  while (size) {
    size_t requested = std::min(size, size_t{32}), received = requested;
    if (sysctl(mib, 2, out, &received, nullptr, 0) || received != requested) return false;
    out += received; size -= received;
  }
#else
  arc4random_buf(out, size);
#endif
  return true;
}
enum class Stage { offline, connect, greeting, reply_head, reply_length, reply_body, tls, login, ready };
int digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
bool hex_bytes(const std::string &hex, std::vector<uint8_t> &bytes) {
  if (hex.empty() || hex.size() % 2 || hex.size() > MOM_PS5_BLOB_MAX * 2) return false;
  bytes.clear();
  for (size_t i = 0; i < hex.size(); i += 2) {
    int hi = digit(hex[i]), lo = digit(hex[i + 1]);
    if (hi < 0 || lo < 0) return false;
    bytes.push_back(static_cast<uint8_t>(hi * 16 + lo));
  }
  return true;
}
bool hex_number(const std::string &text, uint64_t &value) {
  if (text.empty() || text.size() > 16) return false;
  value = 0;
  for (char c : text) {
    int d = digit(c);
    if (d < 0) return false;
    value = (value << 4) | static_cast<unsigned>(d);
  }
  return true;
}
std::string id_text(const Json &value) {
  if (value.is_string()) {
    auto result = value.get<std::string>();
    return result.size() <= MOM_PS5_ID_MAX && result.find('\0') == std::string::npos ? result : "";
  }
  if (value.is_number_unsigned() && value.get<uint64_t>() <= UINT64_C(9007199254740991))
    return std::to_string(value.get<uint64_t>());
  return "";
}
// Bound parser depth before entering the library, including malformed nested input.
bool bounded_json(const std::string &text) {
  if (text.size() > line_limit) return false;
  unsigned depth = 0;
  bool quoted = false, escape = false;
  for (char c : text) {
    if (quoted) {
      if (escape) escape = false;
      else if (c == '\\') escape = true;
      else if (c == '"') quoted = false;
    } else if (c == '"') quoted = true;
    else if (c == '{' || c == '[') { if (++depth > 16) return false; }
    else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
  }
  return !quoted && !depth;
}
bool text_field(const Json &object, const char *name, char *out, size_t capacity, bool required) {
  auto entry = object.find(name);
  if (entry == object.end()) return !required;
  if (!entry->is_string()) return false;
  auto text = entry->get<std::string>();
  if (text.size() >= capacity || text.find('\0') != std::string::npos || (required && text.empty())) return false;
  std::memcpy(out, text.c_str(), text.size() + 1);
  return true;
}
bool uint_field(const Json &object, const char *name, unsigned &out, unsigned maximum) {
  auto entry = object.find(name);
  if (entry == object.end()) return true;
  if (!entry->is_number_unsigned() || entry->get<uint64_t>() > maximum) return false;
  out = entry->get<unsigned>();
  return true;
}
}

struct MomPool {
  MomPoolConfig config{};
  MomManager *manager = nullptr;
  MomPoolLog log = nullptr;
  int fd = -1;
  Stage stage = Stage::offline;
  std::string input, worker;
  Json metadata = Json::object(); // Retain login controls for separately pushed jobs.
  std::deque<std::string> output;
  size_t output_offset = 0, output_bytes = 0, socks_needed = 0;
  uint64_t deadline = 0, last_receive = 0, last_ping = 0;
  uint64_t request_deadline[16]{};
  bool tls_write = false, tls_started = false;
#ifdef MOM_POOL_MBEDTLS
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config ssl_config;
  mbedtls_x509_crt roots;
  mbedtls_ctr_drbg_context random;
#else
  SSL_CTX *ssl_config = nullptr;
  SSL *ssl = nullptr;
#endif
};

extern "C" uint64_t mom_pool_now_ms(void) {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint64_t>(now.tv_sec) * 1000 + static_cast<uint64_t>(now.tv_nsec) / 1000000;
}

extern "C" bool mom_pool_target(const char *text, uint8_t target[32]) {
  if (!text || !target) return false;
  std::string hex(text);
  if (hex.empty() || hex.size() > 64) return false;
  for (char c : hex) if (digit(c) < 0) return false;
  uint64_t value = 0;
  if (hex.size() > 16) {
    if (!hex_number(hex.substr(0, 16), value)) return false;
  } else {
    unsigned bytes = hex.size() <= 8 ? 4 : 8;
    hex.resize(bytes * 2, '0');
    for (unsigned i = 0; i < bytes; ++i)
      value |= static_cast<uint64_t>(digit(hex[i * 2]) * 16 + digit(hex[i * 2 + 1])) << (8 * i);
    if (!value) return false;
    if (bytes == 4) value = UINT64_MAX / (UINT32_MAX / value);
  }
  if (!value) return false;
  std::memset(target, 0, 32);
  for (unsigned i = 0; i < 8; ++i) target[i] = static_cast<uint8_t>(value >> (56 - 8 * i));
  return true;
}

extern "C" bool mom_pool_config_read(const char *path, MomPoolConfig *out) {
  if (!path || !out) return false;
  FILE *file = std::fopen(path, "rb");
  if (!file) return false;
  char buffer[4097];
  size_t size = std::fread(buffer, 1, sizeof(buffer), file);
  bool valid = !std::ferror(file) && size < sizeof(buffer);
  std::fclose(file);
  if (!valid || !bounded_json(std::string(buffer, size))) return false;
  Json json = Json::parse(buffer, buffer + size, nullptr, false);
  if (!json.is_object()) return false;
  MomPoolConfig config{};
  config.runtime_seconds = 600;
  config.tls = true;
  if (!text_field(json, "url", config.host, sizeof(config.host), true) ||
      !text_field(json, "login", config.login, sizeof(config.login), true) ||
      !text_field(json, "pass", config.password, sizeof(config.password), false) ||
      !text_field(json, "proxy_host", config.proxy_host, sizeof(config.proxy_host), false) ||
      !text_field(json, "ca_file", config.ca_file, sizeof(config.ca_file), false) ||
      !uint_field(json, "port", config.port, 65535) || !config.port ||
      !uint_field(json, "proxy_port", config.proxy_port, 65535) ||
      !uint_field(json, "max_graphs", config.max_graphs, 1000000) ||
      !uint_field(json, "runtime_seconds", config.runtime_seconds, 86400)) return false;
  if (json.contains("is_tls")) {
    if (!json["is_tls"].is_boolean()) return false;
    config.tls = json["is_tls"].get<bool>();
  }
  if (json.contains("tls_verify")) {
    if (!json["tls_verify"].is_boolean()) return false;
    config.tls_verify = json["tls_verify"].get<bool>();
  }
  if ((config.proxy_host[0] != 0) != (config.proxy_port != 0)) return false;
  // The sidecar contains no wallet or connection details; corrupt measurements are ignored.
  int length = std::snprintf(config.performance_file, sizeof(config.performance_file), "%s.perf.json", path);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(config.performance_file)) return false;
  FILE *performance = std::fopen(config.performance_file, "rb");
  if (performance) {
    char rate_buffer[257]{};
    size_t rate_size = std::fread(rate_buffer, 1, sizeof(rate_buffer)-1, performance);
    bool readable = !std::ferror(performance) && rate_size < sizeof(rate_buffer)-1;
    std::fclose(performance);
    // PS5's strtod does not satisfy the JSON scanner; integer-only cache parsing also safely ignores v1 decimals.
    const char *cursor=rate_buffer, *end=rate_buffer+rate_size;
    auto literal=[&](const char *text) {
      size_t size=std::strlen(text);
      if (static_cast<size_t>(end-cursor)<size || std::memcmp(cursor,text,size)) return false;
      cursor+=size; return true;
    };
    auto number=[&](uint64_t &value) {
      const char *start=cursor; value=0;
      while (cursor<end && *cursor>='0' && *cursor<='9') {
        unsigned d=static_cast<unsigned>(*cursor-'0');
        if (value>(UINT64_MAX-d)/10) return false;
        value=value*10+d; ++cursor;
      }
      return cursor!=start;
    };
    uint64_t graphs=0, nanogps=0;
    bool parsed=readable && literal("{\"version\":2,\"graphs\":") && number(graphs) &&
      literal(",\"c29_nanogps\":") && number(nanogps) && literal("}");
    while (cursor<end && (*cursor==' ' || *cursor=='\n' || *cursor=='\r' || *cursor=='\t')) ++cursor;
    if (parsed && cursor==end && graphs>=20 && nanogps>0 && nanogps<=UINT64_C(1000000000000))
      config.c29_gps=static_cast<double>(nanogps)/1e9;
  }
  *out = config;
  return true;
}

static void notice(MomPool *p, const char *message) { if (p->log) p->log(message); }
static void transport_error(MomPool *p, const char *stage, int code) {
  // Numeric errors locate native socket failures without exposing endpoint credentials.
  char message[100];
  std::snprintf(message, sizeof(message), "pool transport %s failed: code=%d", stage, code);
  notice(p, message);
}
static void tls_free(MomPool *p) {
#ifdef MOM_POOL_MBEDTLS
  mbedtls_ssl_free(&p->ssl);
  mbedtls_ssl_config_free(&p->ssl_config);
  mbedtls_x509_crt_free(&p->roots);
  mbedtls_ctr_drbg_free(&p->random);
  mbedtls_ssl_init(&p->ssl);
  mbedtls_ssl_config_init(&p->ssl_config);
  mbedtls_x509_crt_init(&p->roots);
  mbedtls_ctr_drbg_init(&p->random);
#else
  if (p->ssl) SSL_free(p->ssl);
  if (p->ssl_config) SSL_CTX_free(p->ssl_config);
  p->ssl = nullptr;
  p->ssl_config = nullptr;
#endif
  p->tls_started = p->tls_write = false;
}
static void disconnect(MomPool *p) {
  if (p->fd >= 0) close(p->fd);
  p->fd = -1;
  tls_free(p);
  p->stage = Stage::offline;
  p->input.clear(); p->output.clear(); p->worker.clear();
  p->metadata = Json::object();
  p->output_bytes = p->output_offset = 0;
  std::memset(p->request_deadline, 0, sizeof(p->request_deadline));
  mom_manager_disconnected(p->manager, mom_pool_now_ms());
  notice(p, "pool disconnected; reconnect scheduled");
}
static bool queue(MomPool *p, const std::string &text) {
  if (text.size() > output_limit - p->output_bytes) { disconnect(p); return false; }
  p->output.push_back(text);
  p->output_bytes += text.size();
  return true;
}
static bool send_json(MomPool *p, const Json &json) { return queue(p, json.dump() + "\n"); }

#ifdef MOM_POOL_MBEDTLS
extern "C" int mbedtls_hardware_poll(void *, unsigned char *out, size_t size, size_t *written) {
  if (!secure_random(out, size)) { *written = 0; return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED; }
  *written = size;
  return 0;
}
static int entropy(void *, unsigned char *out, size_t size) {
  return secure_random(out, size) ? 0 : MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
}
static int tls_send(void *context, const unsigned char *data, size_t size) {
  auto *p = static_cast<MomPool *>(context);
  // Native socket creation can ignore descriptor flags; each operation must remain nonblocking.
  int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
  flags |= MSG_NOSIGNAL;
#endif
  int result = static_cast<int>(send(p->fd, data, size, flags));
  if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return MBEDTLS_ERR_SSL_WANT_WRITE;
  return result < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : result;
}
static int tls_recv(void *context, unsigned char *data, size_t size) {
  auto *p = static_cast<MomPool *>(context);
  int result = static_cast<int>(recv(p->fd, data, size, MSG_DONTWAIT));
  if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return MBEDTLS_ERR_SSL_WANT_READ;
  return result < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : result;
}
#endif

static bool tls_setup(MomPool *p) {
#ifdef MOM_POOL_MBEDTLS
#if defined(MBEDTLS_SSL_PROTO_TLS1_3) || defined(MBEDTLS_USE_PSA_CRYPTO)
  // TLS 1.3 uses PSA key operations even without MBEDTLS_USE_PSA_CRYPTO.
  if (psa_crypto_init() != PSA_SUCCESS) return false;
#endif
  const char *ca = p->config.ca_file[0] ? p->config.ca_file : "/app0/sce_sys/cacert.pem";
  const unsigned char personalization[] = "mom-ps5-tls";
  if (mbedtls_ctr_drbg_seed(&p->random, entropy, nullptr, personalization, sizeof(personalization)) ||
      mbedtls_x509_crt_parse_file(&p->roots, ca) < 0 ||
      mbedtls_ssl_config_defaults(&p->ssl_config, MBEDTLS_SSL_IS_CLIENT,
                                  MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) return false;
  mbedtls_ssl_conf_authmode(&p->ssl_config, p->config.tls_verify ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_ca_chain(&p->ssl_config, &p->roots, nullptr);
  mbedtls_ssl_conf_rng(&p->ssl_config, mbedtls_ctr_drbg_random, &p->random);
  if (mbedtls_ssl_setup(&p->ssl, &p->ssl_config) || mbedtls_ssl_set_hostname(&p->ssl, p->config.host)) return false;
  mbedtls_ssl_set_bio(&p->ssl, p, tls_send, tls_recv, nullptr);
#else
  p->ssl_config = SSL_CTX_new(TLS_client_method());
  if (!p->ssl_config || !SSL_CTX_set_min_proto_version(p->ssl_config, TLS1_2_VERSION)) return false;
  SSL_CTX_set_verify(p->ssl_config, p->config.tls_verify ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
  if (p->config.ca_file[0] ? !SSL_CTX_load_verify_locations(p->ssl_config, p->config.ca_file, nullptr)
                          : !SSL_CTX_set_default_verify_paths(p->ssl_config)) return false;
  p->ssl = SSL_new(p->ssl_config);
  if (!p->ssl || !SSL_set_fd(p->ssl, p->fd) || !SSL_set_tlsext_host_name(p->ssl, p->config.host) ||
      !X509_VERIFY_PARAM_set1_host(SSL_get0_param(p->ssl), p->config.host, 0)) return false;
  SSL_set_mode(p->ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
#endif
  p->tls_started = true;
  return true;
}
static void login(MomPool *p) {
  p->stage = Stage::login;
  p->deadline = mom_pool_now_ms() + 15000;
  Json performance = Json::object();
  // Match MOM's C29 normalization; unknown throughput stays absent until measured.
  if (p->config.c29_gps > 0) performance["c29"] = p->config.c29_gps / 42.0;
  send_json(p, {{"id", 1}, {"jsonrpc", "2.0"}, {"method", "login"},
    {"params", {{"login", p->config.login}, {"pass", p->config.password},
                {"agent", "mom-ps5/0.1"}, {"algo", Json::array({"c29"})},
                {"algo-perf", performance},
                {"extensions", Json::array({"mo-native", "submit-result"})}}}});
}
static void transport_ready(MomPool *p) {
  if (!p->config.tls) { login(p); return; }
  p->stage = Stage::tls;
  if (!tls_setup(p)) { notice(p, "TLS setup failed"); disconnect(p); }
}
static void connected(MomPool *p) {
  notice(p, "pool TCP connected");
  if (p->config.proxy_host[0]) {
    p->stage = Stage::greeting;
    queue(p, std::string("\x05\x01\x00", 3));
    p->socks_needed = 2;
  } else transport_ready(p);
}
static void connect_socket(MomPool *p) {
  const char *host = p->config.proxy_host[0] ? p->config.proxy_host : p->config.host;
  unsigned port = p->config.proxy_host[0] ? p->config.proxy_port : p->config.port;
  addrinfo hints{}, *addresses = nullptr;
  addrinfo numeric{};
  sockaddr_in ipv4{};
  sockaddr_in6 ipv6{};
  // The payload SDK stubs getaddrinfo; numeric proxy addresses need no local DNS.
  if (inet_pton(AF_INET, host, &ipv4.sin_addr) == 1) {
    ipv4.sin_family = AF_INET; ipv4.sin_port = htons(static_cast<uint16_t>(port));
#if defined(__PROSPERO__)
    ipv4.sin_len = sizeof(ipv4);
#endif
    numeric.ai_family = AF_INET; numeric.ai_addr = reinterpret_cast<sockaddr *>(&ipv4);
    numeric.ai_addrlen = sizeof(ipv4); addresses = &numeric;
  } else if (inet_pton(AF_INET6, host, &ipv6.sin6_addr) == 1) {
    ipv6.sin6_family = AF_INET6; ipv6.sin6_port = htons(static_cast<uint16_t>(port));
#if defined(__PROSPERO__)
    ipv6.sin6_len = sizeof(ipv6);
#endif
    numeric.ai_family = AF_INET6; numeric.ai_addr = reinterpret_cast<sockaddr *>(&ipv6);
    numeric.ai_addrlen = sizeof(ipv6); addresses = &numeric;
  }
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  if (!addresses && getaddrinfo(host, std::to_string(port).c_str(), &hints, &addresses)) {
    notice(p, "local address resolution failed; use a numeric SOCKS5 proxy"); disconnect(p); return;
  }
  for (addrinfo *a = addresses; a; a = a->ai_next) {
    // Prefer atomic nonblocking creation, as libuv does; preserve native descriptor flags.
    int fd;
    bool nonblocking = false;
#ifdef SOCK_NONBLOCK
    fd = socket(a->ai_family, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
    nonblocking = fd >= 0;
    if (fd < 0 && errno == EINVAL) fd = socket(a->ai_family, SOCK_STREAM, IPPROTO_TCP);
#else
    fd = socket(a->ai_family, SOCK_STREAM, IPPROTO_TCP);
#endif
    if (fd < 0) { transport_error(p, "socket", errno); continue; }
    // Native sockets require fcntl (FIONBIO is payload-only); poll accepts large handles.
    if (!nonblocking) {
      int flags = fcntl(fd, F_GETFL);
      if (flags < 0 || (!(flags & O_NONBLOCK) && fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)) {
        transport_error(p, "nonblocking", errno); close(fd); continue;
      }
    }
#ifdef SO_NOSIGPIPE
    int enabled = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
    int result = connect(fd, a->ai_addr, a->ai_addrlen);
    if (result == 0 || errno == EINPROGRESS) {
      p->fd = fd;
      p->stage = Stage::connect;
      p->deadline = mom_pool_now_ms() + 30000;
      if (!result) connected(p);
      break;
    }
    transport_error(p, "connect", errno); close(fd);
  }
  if (addresses != &numeric) freeaddrinfo(addresses);
  if (p->fd < 0) disconnect(p);
}
static void socks_read(MomPool *p) {
  unsigned char buffer[300];
  size_t need = p->socks_needed - p->input.size();
  int length = static_cast<int>(recv(p->fd, buffer, std::min(need, sizeof(buffer)), MSG_DONTWAIT));
  if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
  if (length <= 0) { disconnect(p); return; }
  p->input.append(reinterpret_cast<char *>(buffer), static_cast<size_t>(length));
  if (p->input.size() != p->socks_needed) return;
  const auto *data = reinterpret_cast<const unsigned char *>(p->input.data());
  if (p->stage == Stage::greeting) {
    if (data[0] != 5 || data[1] != 0) { disconnect(p); return; }
    std::string request("\x05\x01\x00\x03", 4);
    request += static_cast<char>(std::strlen(p->config.host));
    request += p->config.host;
    request += static_cast<char>(p->config.port >> 8);
    request += static_cast<char>(p->config.port & 255);
    queue(p, request); // Domain ATYP preserves proxy-side DNS for the pool endpoint.
    p->stage = Stage::reply_head; p->socks_needed = 4;
  } else if (p->stage == Stage::reply_head) {
    if (data[0] != 5 || data[1] || data[2]) { transport_error(p, "SOCKS5 reply", data[1]); disconnect(p); return; }
    p->stage = data[3] == 3 ? Stage::reply_length : Stage::reply_body;
    p->socks_needed = data[3] == 1 ? 6 : data[3] == 4 ? 18 : data[3] == 3 ? 1 : 0;
    if (!p->socks_needed) { disconnect(p); return; }
  } else if (p->stage == Stage::reply_length) {
    if (!data[0]) { disconnect(p); return; }
    p->stage = Stage::reply_body; p->socks_needed = data[0] + 2;
  } else { p->input.clear(); notice(p, "SOCKS5 tunnel established"); transport_ready(p); return; }
  p->input.clear();
}

static bool apply_job(MomPool *p, const Json &job, const Json &metadata) {
  if (!job.is_object()) return false;
  auto field = [&](const char *name) -> const Json & {
    static const Json empty;
    auto item = job.find(name);
    if (item != job.end()) return *item;
    item = metadata.find(name);
    return item == metadata.end() ? empty : *item;
  };
  const Json &algo = field("algo");
  if (!algo.is_null() && (!algo.is_string() ||
      (algo != "c29" && algo != "cuckaroo" && algo != "cuckaroo29" && algo != "c29xtm"))) return false;
  std::string id = id_text(field("job_id"));
  const Json &target_field = field("target");
  if (id.empty() || !target_field.is_string()) return false;
  uint8_t target[32];
  if (!mom_pool_target(target_field.get_ref<const std::string &>().c_str(), target)) return false;
  MomJobLayout layout;
  const Json *blob;
  if (field("pre_pow").is_string()) { layout = MOM_JOB_GRIN; blob = &field("pre_pow"); }
  else if (field("blob").is_string()) { layout = MOM_JOB_TARI; blob = &field("blob"); }
  else { layout = MOM_JOB_RAW; blob = &field("blob_hex"); }
  if (!blob->is_string()) return false;
  std::vector<uint8_t> bytes;
  if (!hex_bytes(blob->get_ref<const std::string &>(), bytes)) return false;
  unsigned nonce_bytes = layout == MOM_JOB_GRIN ? 4 : 8, proof_size = 42, offset = 0;
  for (auto spec : {std::pair<const char *, unsigned *>{"noncebytes", &nonce_bytes},
                     {"proofsize", &proof_size}, {"nonceoffset", &offset}}) {
    const Json &value = field(spec.first);
    if (!value.is_null()) {
      if (!value.is_number_unsigned() || value.get<uint64_t>() > MOM_PS5_BLOB_MAX) return false;
      *spec.second = value.get<unsigned>();
    }
  }
  if (nonce_bytes != 4 && nonce_bytes != 8) return false;
  if (layout == MOM_JOB_RAW && field("nonceoffset").is_null()) return false;
  uint64_t nonce, mask = 0;
  if (!secure_random(reinterpret_cast<unsigned char *>(&nonce), sizeof(nonce))) return false;
  nonce &= nonce_bytes == 4 ? UINT32_MAX >> 1 : UINT64_MAX >> 1;
  const Json &specified = field("nonce");
  bool explicit_nonce = !specified.is_null();
  if (explicit_nonce) {
    if (specified.is_string()) { if (!hex_number(specified.get_ref<const std::string &>(), nonce)) return false; }
    else if (specified.is_number_unsigned()) nonce = specified.get<uint64_t>();
    else return false;
  }
  const Json &mask_field = field("nicehash_mask");
  if (!mask_field.is_null() && (!mask_field.is_string() ||
      !hex_number(mask_field.get_ref<const std::string &>(), mask))) return false;
  const Json &xn = field("xn");
  if (!xn.is_null()) {
    if (!xn.is_string()) return false;
    const auto &prefix = xn.get_ref<const std::string &>();
    uint64_t value;
    if (prefix.size() % 2 || prefix.size() > nonce_bytes * 2) return false;
    // An empty prefix protects no bits, without a full-width shift by 64.
    if (prefix.empty()) mask = 0;
    else {
      if (!hex_number(prefix, value)) return false;
      unsigned suffix = nonce_bytes * 8 - static_cast<unsigned>(prefix.size() * 4);
      mask = (nonce_bytes == 4 ? UINT32_MAX : UINT64_MAX) & (UINT64_MAX << suffix);
      value <<= suffix;
      if (explicit_nonce && (nonce & mask) != value) return false;
      nonce = value | (nonce & ~mask);
    }
  }
  MomJob normalized;
  return mom_job_build(&normalized, id.c_str(), layout, bytes.data(), bytes.size(),
                       nonce_bytes, offset, proof_size, target, nonce, mask) &&
         mom_manager_job(p->manager, &normalized);
}

static bool process_line(MomPool *p, const std::string &line) {
  if (!bounded_json(line)) return false;
  Json json = Json::parse(line, nullptr, false);
  if (!json.is_object()) return false;
  auto id = json.find("id"), method = json.find("method"), result = json.find("result");
  bool error = json.contains("error") && !json["error"].is_null();
  if (p->stage == Stage::login) {
    if (error) {
      // Report only a numeric server error; error text may echo account identifiers.
      const auto &failure = json["error"];
      if (failure.is_object() && failure.contains("code") && failure["code"].is_number_integer()) {
        char message[80];
        std::snprintf(message, sizeof(message), "pool login error: code=%lld", failure["code"].get<long long>());
        notice(p, message);
      }
    }
    if (id == json.end() || *id != 1 || error || result == json.end() || !result->is_object() ||
        !result->contains("id")) return false;
    p->worker = id_text((*result)["id"]);
    p->metadata = *result;
    if (p->worker.empty()) { notice(p, "pool worker ID rejected"); return false; }
    if (!mom_manager_connected(p->manager)) { notice(p, "pool connection state rejected"); return false; }
    if (result->contains("job") && !apply_job(p, (*result)["job"], *result)) {
      notice(p, "pool login job is unsupported or malformed"); return false;
    }
    p->stage = Stage::ready;
    p->last_ping = mom_pool_now_ms();
    notice(p, "pool authenticated");
    return true;
  }
  if (method != json.end()) {
    if (*method == "mining.set_extranonce") {
      // MoneroOcean sends the C29 prefix before its first separate job notification.
      if (!json.contains("params") || !json["params"].is_array() || json["params"].size() != 2) return false;
      const auto &params = json["params"];
      if (!params[0].is_string() || !params[1].is_number_unsigned() || params[1].get<uint64_t>() > 8) return false;
      const auto &prefix = params[0].get_ref<const std::string &>();
      if (prefix.size() > 16 || prefix.size() % 2) return false;
      for (char c : prefix) if (digit(c) < 0) return false;
      p->metadata["xn"] = prefix;
      p->metadata["extra_nonce2_size"] = params[1];
      return true;
    }
    if (*method != "job") return false;
    bool ready = json.contains("params") && apply_job(p, json["params"], p->metadata);
    if (ready) notice(p, "C29 job ready");
    return ready;
  }
  if (id != json.end() && id->is_number_unsigned() && id->get<uint64_t>() >= 101) {
    uint64_t request = id->get<uint64_t>() - 100;
    bool accepted = !error && result != json.end() &&
      (*result == true || (result->is_object() && result->contains("status") && (*result)["status"] == "OK"));
    for (unsigned i = 0; i < 16; ++i) {
      if (p->manager->pending[i] != request) continue;
      if (mom_manager_share_reply(p->manager, p->manager->session, request, accepted)) {
        p->request_deadline[i] = 0;
        notice(p, accepted ? "pool share accepted" : "pool share rejected");
      }
      break;
    }
  }
  return true;
}

static int io(MomPool *p, void *buffer, size_t size, bool write) {
  if (p->tls_started && (p->stage == Stage::login || p->stage == Stage::ready)) {
#ifdef MOM_POOL_MBEDTLS
    int result = write ? mbedtls_ssl_write(&p->ssl, static_cast<const unsigned char *>(buffer), size)
                       : mbedtls_ssl_read(&p->ssl, static_cast<unsigned char *>(buffer), size);
    if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) return -2;
    return result < 0 ? -1 : result;
#else
    int result = write ? SSL_write(p->ssl, buffer, static_cast<int>(size)) : SSL_read(p->ssl, buffer, static_cast<int>(size));
    if (result > 0) return result;
    int error = SSL_get_error(p->ssl, result);
    return error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE ? -2 : -1;
#endif
  }
  int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
  if (write) flags |= MSG_NOSIGNAL;
#endif
  int result = static_cast<int>(write ? send(p->fd, buffer, size, flags) : recv(p->fd, buffer, size, 0));
  return result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? -2 : result;
}

extern "C" MomPool *mom_pool_create(const MomPoolConfig *config, MomManager *manager, MomPoolLog log) {
  if (!config || !manager || !config->host[0] || !config->port || config->port > 65535 ||
      !config->login[0] || config->proxy_port > 65535) return nullptr;
  MomPool *p = new MomPool;
  p->config = *config; p->manager = manager; p->log = log;
#ifdef MOM_POOL_MBEDTLS
  mbedtls_ssl_init(&p->ssl); mbedtls_ssl_config_init(&p->ssl_config);
  mbedtls_x509_crt_init(&p->roots); mbedtls_ctr_drbg_init(&p->random);
#endif
  return p;
}
extern "C" void mom_pool_destroy(MomPool *p) {
  if (!p) return;
  if (p->fd >= 0) close(p->fd);
  tls_free(p);
  mom_manager_stop(p->manager);
  delete p;
}

extern "C" void mom_pool_pump(MomPool *p, unsigned timeout) {
  if (!p || p->manager->stopped) return;
  uint64_t now = mom_pool_now_ms();
  if (p->fd < 0) {
    if (mom_manager_retry_due(p->manager, now)) connect_socket(p);
    else poll(nullptr, 0, static_cast<int>(std::min(timeout, 100u)));
    if (p->fd < 0) return;
  }
  if (p->stage != Stage::ready && now > p->deadline) { notice(p, "pool handshake timed out"); disconnect(p); return; }
  if (p->stage == Stage::ready) {
    if (now - p->last_receive > 90000) { disconnect(p); return; }
    for (unsigned i = 0; i < 16; ++i)
      if (p->request_deadline[i] && now > p->request_deadline[i]) { disconnect(p); return; }
    if (now - p->last_ping >= 30000) {
      p->last_ping = now;
      if (!send_json(p, {{"id", 2}, {"jsonrpc", "2.0"}, {"method", "keepalived"}, {"params", {{"id", p->worker}}}})) return;
    }
  }
  pollfd event{p->fd, POLLIN, 0};
  if (p->stage == Stage::connect || !p->output.empty() || (p->stage == Stage::tls && p->tls_write)) event.events |= POLLOUT;
  int selected = poll(&event, 1, static_cast<int>(std::min(timeout, 1000u)));
  if (selected < 0) { if (errno != EINTR) disconnect(p); return; }
  if (event.revents & POLLNVAL) { transport_error(p, "poll", EBADF); disconnect(p); return; }
  if (p->stage == Stage::connect && (event.revents & (POLLOUT | POLLERR | POLLHUP))) {
    int error = 0; socklen_t size = sizeof(error);
    if (getsockopt(p->fd, SOL_SOCKET, SO_ERROR, &error, &size) || error) {
      transport_error(p, "connect completion", error ? error : errno); disconnect(p); return;
    }
    connected(p);
    if (p->fd < 0) return;
  }
  if (p->stage == Stage::tls) {
#ifdef MOM_POOL_MBEDTLS
    int result = mbedtls_ssl_handshake(&p->ssl);
    p->tls_write = result == MBEDTLS_ERR_SSL_WANT_WRITE;
    if (!result && (!p->config.tls_verify || !mbedtls_ssl_get_verify_result(&p->ssl))) {
      notice(p, p->config.tls_verify ? "TLS certificate verified" : "TLS connected; MOM certificate policy"); login(p);
    }
    else if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
      // Numeric TLS diagnostics exclude pool credentials and job contents.
      char message[128];
      std::snprintf(message, sizeof(message), "TLS failed: error=%d verify=%u", result,
                    static_cast<unsigned>(mbedtls_ssl_get_verify_result(&p->ssl)));
      notice(p, message); disconnect(p);
    }
#else
    int result = SSL_connect(p->ssl);
    int error = result == 1 ? 0 : SSL_get_error(p->ssl, result);
    p->tls_write = error == SSL_ERROR_WANT_WRITE;
    if (result == 1 && (!p->config.tls_verify || SSL_get_verify_result(p->ssl) == X509_V_OK)) {
      notice(p, p->config.tls_verify ? "TLS certificate verified" : "TLS connected; MOM certificate policy"); login(p);
    }
    else if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
      // OpenSSL error codes reveal transport failures without logging account data.
      char message[160];
      std::snprintf(message, sizeof(message), "TLS failed: error=%d verify=%ld reason=%lu errno=%d",
                    error, SSL_get_verify_result(p->ssl), ERR_peek_last_error(), errno);
      notice(p, message); disconnect(p);
    }
#endif
    if (p->fd < 0 || p->stage == Stage::tls) return;
  }
  if (!p->output.empty()) {
    std::string &front = p->output.front();
    int result = io(p, front.data() + p->output_offset, front.size() - p->output_offset, true);
    if (result == -2) return;
    if (result <= 0) { disconnect(p); return; }
    p->output_offset += static_cast<size_t>(result);
    p->output_bytes -= static_cast<size_t>(result);
    if (p->output_offset == front.size()) { p->output.pop_front(); p->output_offset = 0; }
  }
  if (p->stage == Stage::greeting || p->stage == Stage::reply_head ||
      p->stage == Stage::reply_length || p->stage == Stage::reply_body) { socks_read(p); return; }
  if (p->stage != Stage::login && p->stage != Stage::ready) return;
  char buffer[4096];
  int length = io(p, buffer, sizeof(buffer), false);
  if (length == -2) return;
  if (length <= 0) { disconnect(p); return; }
  p->last_receive = now;
  p->input.append(buffer, static_cast<size_t>(length));
  for (;;) {
    size_t end = p->input.find('\n');
    if (end == std::string::npos) break;
    std::string line = p->input.substr(0, end);
    p->input.erase(0, end + 1);
    if (!line.empty() && !process_line(p, line)) { notice(p, "pool message rejected"); disconnect(p); return; }
  }
  if (p->input.size() > line_limit) { notice(p, "pool message too large"); disconnect(p); }
}

extern "C" uint64_t mom_pool_submit(MomPool *p, const MomWork *work, const uint32_t *nonces, size_t count) {
  if (!p || p->stage != Stage::ready || p->manager->next_share >= UINT64_MAX - 100) return 0;
  uint8_t hash[32];
  uint64_t request = mom_manager_share(p->manager, work, nonces, count, hash);
  if (!request) return 0;
  char nonce[17], digest[65];
  std::snprintf(nonce, sizeof(nonce), "%0*llx", static_cast<int>(work->job.nonce_bytes * 2), static_cast<unsigned long long>(work->nonce));
  for (unsigned i = 0; i < 32; ++i) std::snprintf(digest + 2 * i, 3, "%02x", hash[i]);
  Json params = {{"id", p->worker}, {"job_id", work->job.id}, {"nonce", nonce},
                  {"result", digest}, {"pow", std::vector<uint32_t>(nonces, nonces + count)}};
  if (count == 32) params["nonce"] = work->nonce;
  if (!send_json(p, {{"id", request + 100}, {"jsonrpc", "2.0"}, {"method", "submit"}, {"params", params}})) return 0;
  for (unsigned i = 0; i < 16; ++i)
    if (p->manager->pending[i] == request) p->request_deadline[i] = mom_pool_now_ms() + 45000;
  notice(p, "verified share queued");
  return request;
}
extern "C" bool mom_pool_pending(const MomPool *p) {
  if (!p) return false;
  for (uint64_t pending : p->manager->pending) if (pending) return true;
  return false;
}

extern "C" bool mom_pool_record_performance(MomPool *p, double gps, uint64_t graphs) {
  if (!p || graphs < 20 || !std::isfinite(gps) || gps <= 0 || gps > 1000) return false;
  p->config.c29_gps = gps; // Reconnects in this process use the latest actual measurement.
  if (!p->config.performance_file[0]) return false;
  std::string temporary = std::string(p->config.performance_file) + ".tmp";
  FILE *file = std::fopen(temporary.c_str(), "wb");
  if (!file) return false;
  int written = std::fprintf(file, "{\"version\":2,\"graphs\":%llu,\"c29_nanogps\":%llu}\n",
      static_cast<unsigned long long>(graphs), static_cast<unsigned long long>(gps*1e9+.5));
  bool valid = written > 0 && !std::ferror(file);
  if (std::fclose(file)) valid = false;
  if (valid && !std::rename(temporary.c_str(), p->config.performance_file)) return true;
  std::remove(temporary.c_str());
  return false;
}
