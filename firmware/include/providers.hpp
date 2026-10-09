#pragma once
#include "app.hpp"
#include "net.hpp"
struct LibreCredentials {
    String user, password, region = "eu", version = "5.1.1";
};
bool libreListConnections(const LibreCredentials &credentials, ConnectionChoice *out,
                          size_t capacity, size_t &count, String &resolvedRegion, String &error);
class LibreClient {
  public:
    uint32_t listConnections(AppState &state);
    uint32_t read(AppState &state);
    void resetSession();

  private:
    String token, account, region, identity;
    bool login(String &error, uint32_t &retry);
    net::Response get(const String &route, net::Doc &doc, size_t responseLimit = 96 * 1024,
                      JsonVariantConst filter = JsonVariantConst());
};
void connectionCacheLoad(AppState &state);
void connectionCacheSave(const AppState &state);
