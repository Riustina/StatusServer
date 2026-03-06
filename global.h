// global.h

#pragma once
//namespace beast = boost::beast;         // from <boost/beast.hpp>
//namespace http = beast::http;           // from <boost/beast/http.hpp>
//namespace net = boost::asio;            // from <boost/asio.hpp>
//using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

enum ErrorCodes {
	Success = 0,
	Error_Json = 1001,
	RPC_Failed = 1002,
	VerifyExpired = 1003,
	VerifyCodeError = 1004,
	UserExists = 1005,
	PasswdError = 1006,
	EmailNotMatch = 1007,
	PasswdUpFailed = 1008,
	PasswdInvalid = 1009,
	TokenInvalid = 1010,   //Token ß–ß
	UidInvalid = 1011,  //uidŒﬁ–ß



	MySQLFailed = 9999,
};
