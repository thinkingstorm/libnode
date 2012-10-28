// Copyright (c) 2012 Plenluno All rights reserved.

#ifndef LIBNODE_HTTP_SERVER_RESPONSE_H_
#define LIBNODE_HTTP_SERVER_RESPONSE_H_

#include "libnode/stream/writable_stream.h"

namespace libj {
namespace node {
namespace http {

class ServerResponse : LIBNODE_WRITABLE_STREAM(ServerResponse)
 public:
    virtual void writeHead(
        Int statusCode,
        String::CPtr reasonPhrase = String::null(),
        JsObject::CPtr headers = JsObject::null()) = 0;
    virtual Int statusCode() const = 0;
    virtual void setHeader(String::CPtr name, String::CPtr value) = 0;
    virtual String::CPtr getHeader(String::CPtr name) const = 0;
    virtual void removeHeader(String::CPtr name) = 0;
};

#define LIBNODE_HTTP_SERVER_RESPONSE(T) \
    public libj::node::http::ServerResponse { \
    LIBJ_MUTABLE_DEFS(T, libj::node::http::ServerResponse)

}  // namespace http
}  // namespace node
}  // namespace libj

#endif  // LIBNODE_HTTP_SERVER_RESPONSE_H_
