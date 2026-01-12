#pragma once
#include <string>
#include <json/json.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <drogon/utils/Utilities.h>

namespace realworld_utils {

class JWT {
public:
    static std::string encode(int userId, const std::string& username, const std::string& email) {
        Json::Value header;
        header["alg"] = "HS256";
        header["typ"] = "JWT";
        
        Json::Value payload;
        payload["userId"] = userId;
        payload["username"] = username;
        payload["email"] = email;
        payload["exp"] = static_cast<Json::Int64>(std::time(nullptr) + 86400 * 60); // 60 days
        
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        
        std::string headerStr = Json::writeString(builder, header);
        std::string payloadStr = Json::writeString(builder, payload);
        
        std::string headerB64 = drogon::utils::base64Encode((const unsigned char*)headerStr.c_str(), headerStr.length());
        std::string payloadB64 = drogon::utils::base64Encode((const unsigned char*)payloadStr.c_str(), payloadStr.length());
        
        // Remove padding
        headerB64.erase(std::remove(headerB64.begin(), headerB64.end(), '='), headerB64.end());
        payloadB64.erase(std::remove(payloadB64.begin(), payloadB64.end(), '='), payloadB64.end());
        
        std::string message = headerB64 + "." + payloadB64;
        std::string signature = sign(message);
        
        return message + "." + signature;
    }
    
    static bool decode(const std::string& token, int& userId, std::string& username, std::string& email) {
        size_t firstDot = token.find('.');
        size_t secondDot = token.find('.', firstDot + 1);
        
        if (firstDot == std::string::npos || secondDot == std::string::npos) {
            return false;
        }
        
        std::string headerB64 = token.substr(0, firstDot);
        std::string payloadB64 = token.substr(firstDot + 1, secondDot - firstDot - 1);
        std::string signature = token.substr(secondDot + 1);
        
        std::string message = headerB64 + "." + payloadB64;
        std::string expectedSignature = sign(message);
        
        if (signature != expectedSignature) {
            return false;
        }
        
        // Add padding if needed
        while (payloadB64.length() % 4 != 0) {
            payloadB64 += "=";
        }
        
        auto payloadBytes = drogon::utils::base64Decode(payloadB64);
        std::string payloadStr(payloadBytes.begin(), payloadBytes.end());
        
        Json::Value payload;
        Json::CharReaderBuilder reader;
        std::istringstream s(payloadStr);
        std::string errs;
        
        if (!Json::parseFromStream(reader, s, &payload, &errs)) {
            return false;
        }
        
        if (payload["exp"].asInt64() < std::time(nullptr)) {
            return false;
        }
        
        userId = payload["userId"].asInt();
        username = payload["username"].asString();
        email = payload["email"].asString();
        
        return true;
    }
    
private:
    static std::string sign(const std::string& message) {
        const std::string secret = "your-secret-key-change-in-production";
        
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digestLen;
        
        HMAC(EVP_sha256(),
             secret.c_str(), secret.length(),
             (unsigned char*)message.c_str(), message.length(),
             digest, &digestLen);
        
        std::string result = drogon::utils::base64Encode(digest, digestLen);
        result.erase(std::remove(result.begin(), result.end(), '='), result.end());
        
        return result;
    }
};

class Password {
public:
    static std::string hash(const std::string& password) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256((unsigned char*)password.c_str(), password.length(), hash);
        
        std::ostringstream ss;
        for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
    }
    
    static bool verify(const std::string& password, const std::string& hashedPassword) {
        return hash(password) == hashedPassword;
    }
};

}
