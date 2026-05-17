#pragma once

#include "cookie_jar.hpp"
#include "url.hpp"

#include <boost/beast/http.hpp>

#include <string_view>

namespace hisol {

namespace http = boost::beast::http;

void log_http_request(const http::request<http::string_body>& request, bool enabled);
void log_http_response(const http::response<http::string_body>& response, std::string_view decoded_body, bool enabled);
void log_websocket_request(const Url& url, const CookieJar& cookies, bool enabled);
void log_websocket_response(const http::response<http::string_body>& response, bool enabled);

} // namespace hisol
