#include <MemoryManager.h>
#include <OpdsClient.h>

#include <cassert>
#include <cstdio>
#include <string>

using namespace freeink::opds;
class Transport : public OpdsTransport {
 public:
  std::string body =
      "<feed><entry><title>Book</title><link type=\"application/atom+xml\" href=\"/book\"/></entry></feed>";
  bool complete = true;
  bool requireBasic = false;
  int calls = 0;
  int feedCalls = 0;
  HttpResult getFeed(const std::string& url, const HttpAuth& auth, const char* accept, const char* language,
                     bool capture, const OpdsDataSink& sink) override {
    ++feedCalls;
    return get(url, auth, accept, language, capture, sink);
  }
  HttpResult get(const std::string&, const HttpAuth& auth, const char*, const char*, bool capture,
                 const OpdsDataSink& sink) override {
    ++calls;
    if (requireBasic && auth.username.empty()) {
      if (capture) sink(reinterpret_cast<const uint8_t*>(""), 0);
      return {false, 401};
    }
    bool accepted = sink(reinterpret_cast<const uint8_t*>(body.data()), body.size());
    return {complete && accepted, 200};
  }
  bool postForm(const std::string&, const std::string&, std::string&, int&) override { return false; }
  std::string resolveUrl(const std::string&, const std::string& ref) override { return ref; }
};
int main() {
  using Status = OpdsClient::FetchStatus;
  Transport transport;
  OpdsClient client(transport);
  {
    OpdsFeedParser parser;
    assert(client.fetchFeed("/feed", parser) == Status::Ok);
    assert(transport.feedCalls == 1);
  }
  // A lost closing tag is a network failure when the response was interrupted.
  transport.body.resize(transport.body.size() - 7);
  transport.complete = false;
  {
    OpdsFeedParser parser;
    assert(client.fetchFeed("/feed", parser) == Status::FetchFailed);
  }
  transport.complete = true;
  {
    OpdsFeedParser parser;
    assert(client.fetchFeed("/feed", parser) == Status::ParseFailed);
  }
  // Syntax errors that abort the sink still report parser failure.
  transport.body = "<feed><broken></feed>";
  {
    OpdsFeedParser parser;
    assert(client.fetchFeed("/feed", parser) == Status::ParseFailed);
  }
  freeink::testFree = 1;
  {
    OpdsFeedParser parser;
    assert(client.fetchFeed("/feed", parser) == Status::MemoryError);
  }
  freeink::testFree = 1000000;
  // Authentication resets the backend without losing the requested window.
  transport.body =
      "<feed><entry><title>First</title><link type=\"application/atom+xml\" "
      "href=\"/one\"/></entry><entry><title>Second</title><link type=\"application/atom+xml\" "
      "href=\"/two\"/></entry></feed>";
  transport.requireBasic = true;
  client.setServer("/feed", "user", "password");
  {
    OpdsFeedParser parser;
    parser.setWindow({1, SIZE_MAX, 128});
    assert(client.fetchFeed("/feed", parser) == Status::Ok);
    auto entries = parser.takeEntries();
    assert(entries.size() == 1 && entries[0].href == "/two");
  }
  puts("OPDS client: interrupted transfer, malformed feed, memory failure and Basic retry passed");
}
