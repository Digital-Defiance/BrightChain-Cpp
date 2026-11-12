#include "brightchain/v2/block_source.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <thread>

#include "brightchain/v2/disk_store.hpp"

namespace brightchain::v2 {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace {

bool isDigest(const std::string& s) {
  if (s.size() != 128) return false;
  for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

}  // namespace

HttpBlockSource::HttpBlockSource(std::string baseUrl, std::optional<std::string> token) : token_(std::move(token)) {
  const std::string scheme = "http://";
  if (baseUrl.compare(0, scheme.size(), scheme) != 0) throw StorageError("Only http:// block sources are supported");
  auto rest = baseUrl.substr(scheme.size());
  auto slash = rest.find('/');
  auto authority = rest.substr(0, slash);
  prefix_ = slash == std::string::npos ? "" : rest.substr(slash);
  while (!prefix_.empty() && prefix_.back() == '/') prefix_.pop_back();
  auto colon = authority.rfind(':');
  if (!authority.empty() && authority.front() == '[') {  // [::1]:8080
    auto close = authority.find(']');
    host_ = authority.substr(1, close - 1);
    port_ = close + 1 < authority.size() && authority[close + 1] == ':' ? authority.substr(close + 2) : "80";
  } else if (colon != std::string::npos) {
    host_ = authority.substr(0, colon);
    port_ = authority.substr(colon + 1);
  } else {
    host_ = authority;
    port_ = "80";
  }
}

namespace {

/// One GET, returning status and body.
std::pair<int, Bytes> get(const std::string& host, const std::string& port, const std::string& target,
                          const std::optional<std::string>& token) {
  net::io_context ioc;
  tcp::resolver resolver(ioc);
  beast::tcp_stream stream(ioc);
  stream.expires_after(std::chrono::seconds(30));
  stream.connect(resolver.resolve(host, port));
  http::request<http::empty_body> request{http::verb::get, target, 11};
  request.set(http::field::host, host);
  request.set(http::field::connection, "close");
  if (token) request.set(http::field::authorization, "Bearer " + *token);
  http::write(stream, request);
  beast::flat_buffer buffer;
  http::response_parser<http::vector_body<std::uint8_t>> parser;
  parser.body_limit(kMaxDocumentBytes + 4096);
  http::read(stream, buffer, parser);
  beast::error_code ignored;
  stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
  return {int(parser.get().result_int()), std::move(parser.get().body())};
}

}  // namespace

Digest HttpBlockSource::readHead() const {
  auto [status, body] = get(host_, port_, prefix_ + "/head", token_);
  if (status != 200) throw StorageError("Cannot read the remote head (HTTP " + std::to_string(status) + ")");
  std::string head(body.begin(), body.end());
  while (!head.empty() && (head.back() == '\n' || head.back() == '\r')) head.pop_back();
  if (!isDigest(head)) throw StorageError("The remote head is not a digest");
  return head;
}

Bytes HttpBlockSource::readBlock(const Digest& digest) const {
  if (!isDigest(digest)) throw StorageError("Invalid block digest");
  auto [status, body] = get(host_, port_, prefix_ + "/blocks/" + digest, token_);
  if (status == 404) throw MissingBlockError(digest);
  if (status != 200) throw StorageError("Cannot read remote block " + digest + " (HTTP " + std::to_string(status) + ")");
  if (digestPayload(body) != digest) throw StorageError("Remote block failed its digest check: " + digest);
  return body;
}

struct BlockServer::Impl {
  net::io_context ioc{1};
  tcp::acceptor acceptor{ioc};
  std::shared_ptr<const BlockSource> source;
  std::optional<std::string> token;
  std::thread thread;
  bool stopped = false;

  void accept() {
    acceptor.async_accept([this](beast::error_code ec, tcp::socket socket) {
      if (ec) return;
      try { handle(socket); } catch (...) {}
      accept();
    });
  }

  void handle(tcp::socket& socket) {
    beast::flat_buffer buffer;
    http::request<http::string_body> request;
    http::read(socket, buffer, request);
    auto reply = [&](http::status status, const std::string& body, const char* type = "text/plain") {
      http::response<http::string_body> response{status, request.version()};
      response.set(http::field::content_type, type);
      response.keep_alive(false);
      response.body() = body;
      response.prepare_payload();
      http::write(socket, response);
    };
    if (request.method() != http::verb::get) return reply(http::status::method_not_allowed, "");
    if (token && std::string(request[http::field::authorization]) != "Bearer " + *token)
      return reply(http::status::unauthorized, "");
    std::string path(request.target());
    path = path.substr(0, path.find('?'));
    try {
      if (path == "/head") return reply(http::status::ok, source->readHead() + "\n");
      const std::string prefix = "/blocks/";
      if (path.compare(0, prefix.size(), prefix) == 0 && isDigest(path.substr(prefix.size()))) {
        auto bytes = source->readBlock(path.substr(prefix.size()));
        return reply(http::status::ok, std::string(bytes.begin(), bytes.end()), "application/octet-stream");
      }
      return reply(http::status::not_found, "");
    } catch (const MissingBlockError&) {
      return reply(http::status::not_found, "");
    } catch (...) {
      return reply(http::status::internal_server_error, "");
    }
  }
};

std::unique_ptr<BlockServer> BlockServer::start(std::shared_ptr<const BlockSource> source, unsigned short port,
                                                const std::string& host, std::optional<std::string> token) {
  std::unique_ptr<BlockServer> server(new BlockServer());
  server->impl_ = std::make_unique<Impl>();
  auto& impl = *server->impl_;
  impl.source = std::move(source);
  impl.token = std::move(token);
  tcp::endpoint endpoint(net::ip::make_address(host), port);
  impl.acceptor.open(endpoint.protocol());
  impl.acceptor.set_option(net::socket_base::reuse_address(true));
  impl.acceptor.bind(endpoint);
  impl.acceptor.listen();
  auto bound = impl.acceptor.local_endpoint();
  auto address = bound.address().to_string();
  server->url_ = "http://" + (address.find(':') != std::string::npos ? "[" + address + "]" : address) + ":" +
                 std::to_string(bound.port());
  impl.accept();
  impl.thread = std::thread([&impl] { impl.ioc.run(); });
  return server;
}

void BlockServer::stop() {
  if (!impl_ || impl_->stopped) return;
  impl_->stopped = true;
  impl_->ioc.stop();
  if (impl_->thread.joinable()) impl_->thread.join();
  beast::error_code ignored;
  impl_->acceptor.close(ignored);
}

BlockServer::~BlockServer() { stop(); }

}  // namespace brightchain::v2
