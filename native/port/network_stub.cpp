/* Replacement for Rack's src/network.cpp (libcurl-based).
 *
 * Android builds don't link curl/openssl yet: the plugin library/account
 * features that use HTTP are desktop-oriented and are out of scope until
 * phase 5 (see PORTING.md). Requests fail gracefully: callers in Rack already
 * handle NULL/false returns as "network unavailable".
 */
#include <network.hpp>
#include <string.hpp>


namespace rack {
namespace network {


void init() {}

void destroy() {}

json_t* requestJson(Method method, const std::string& url, json_t* dataJ, const CookieMap& cookies) {
	(void) method;
	(void) dataJ;
	(void) cookies;
	WARN("network::requestJson(%s) unavailable in this build", url.c_str());
	return NULL;
}

bool requestDownload(const std::string& url, const std::string& filename, float* progress, const CookieMap& cookies) {
	(void) filename;
	(void) cookies;
	if (progress)
		*progress = 0.f;
	WARN("network::requestDownload(%s) unavailable in this build", url.c_str());
	return false;
}

std::string encodeUrl(const std::string& s) {
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	for (char c : s) {
		if (std::isalnum((unsigned char) c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out += c;
		}
		else {
			out += '%';
			out += hex[((unsigned char) c) >> 4];
			out += hex[((unsigned char) c) & 0x0f];
		}
	}
	return out;
}

std::string urlPath(const std::string& url) {
	// Skip "scheme://authority", return from the next '/' onward.
	size_t schemeEnd = url.find("://");
	size_t start = (schemeEnd == std::string::npos) ? 0 : schemeEnd + 3;
	size_t pathStart = url.find('/', start);
	if (pathStart == std::string::npos)
		return "";
	return url.substr(pathStart);
}


} // namespace network
} // namespace rack
