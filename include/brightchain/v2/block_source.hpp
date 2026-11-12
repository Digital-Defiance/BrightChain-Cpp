#pragma once
// Block sources: where a merge reads another replica from. A source needs only its current root
// and any block by digest. Every block an HTTP source returns is verified against its digest.

#include <memory>
#include <optional>
#include <string>

#include "brightchain/v2/codec.hpp"

namespace brightchain::v2 {

class BlockSource {
 public:
  virtual ~BlockSource() = default;
  /// The replica's current root digest.
  virtual Digest readHead() const = 0;
  /// A block's plaintext payload; throws MissingBlockError when absent.
  virtual Bytes readBlock(const Digest& digest) const = 0;
};

/// Where new blocks are written. A DiskStore writer is one; tests can supply others.
class BlockWriter {
 public:
  virtual ~BlockWriter() = default;
  /// Store a payload and return its digest.
  virtual Digest writeBlock(const Bytes& payload) = 0;
  /// Make blocks written so far durable without publishing a head.
  virtual void syncBlocks() = 0;
};

/// Reads a replica served by BlockServer (or anything with `GET /head` and `GET /blocks/<digest>`).
/// Only http:// URLs are supported; use a TLS-terminating proxy on untrusted networks.
class HttpBlockSource : public BlockSource {
 public:
  explicit HttpBlockSource(std::string baseUrl, std::optional<std::string> token = std::nullopt);
  Digest readHead() const override;
  Bytes readBlock(const Digest& digest) const override;

 private:
  std::string host_, port_, prefix_;
  std::optional<std::string> token_;
};

/// Serves a source read-only over HTTP. It returns blocks as the source reads them (plaintext for
/// an encrypted directory), so set a token and use TLS whenever the network is not trusted.
class BlockServer {
 public:
  static std::unique_ptr<BlockServer> start(std::shared_ptr<const BlockSource> source, unsigned short port = 0,
                                            const std::string& host = "127.0.0.1",
                                            std::optional<std::string> token = std::nullopt);
  ~BlockServer();
  BlockServer(const BlockServer&) = delete;
  BlockServer& operator=(const BlockServer&) = delete;
  /// Base URL to give to HttpBlockSource.
  const std::string& url() const { return url_; }
  void stop();

 private:
  BlockServer() = default;
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string url_;
};

}  // namespace brightchain::v2
