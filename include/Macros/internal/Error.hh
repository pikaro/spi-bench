#pragma once

#include "Base.hh"

#define INTERNAL_ERR_DEFAULT(err) ReturnCode::from(DefaultError::err)
#define INTERNAL_ERR_DOMAIN(domain, err) ReturnCode::from(domain::err)
#define ERR(...)                                                               \
    INTERNAL_GET_MACRO_2(__VA_ARGS__, INTERNAL_ERR_DOMAIN,                     \
                         INTERNAL_ERR_DEFAULT)(__VA_ARGS__)

#define INTERNAL_OK_DEFAULT() ReturnCode::from(DefaultError::Ok)
#define INTERNAL_OK_DOMAIN(domain) ReturnCode::from(domain::Ok)
#define OK(...)                                                                \
    INTERNAL_GET_MACRO_1(__VA_ARGS__ __VA_OPT__(, ) INTERNAL_OK_DOMAIN,        \
                         INTERNAL_OK_DEFAULT)(__VA_ARGS__)
