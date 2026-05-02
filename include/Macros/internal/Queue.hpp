#pragma once

#define STANDARD_QUEUE(queueName, elemType, queueSize)                         \
    Totem::Queue::Platform::Storage<elemType, queueSize> CONCAT(queueName,     \
                                                                Storage);      \
    Totem::Queue::Handle queueName = nullptr;

#define INIT_QUEUE_OR_FAIL(queueName)                                          \
    do {                                                                       \
        auto _result_ =                                                        \
            Totem::Queue::Platform::create(CONCAT(queueName, Storage));        \
        FAIL_IF_ASSIGN_UNEXPECTED_FWD(queueName, _result_,                     \
                                      "Failed to create queue " #queueName     \
                                      ": " ERR_FMT,                            \
                                      ERR_ARG(_result_.error()));              \
    } while (0)

#define DESTROY_QUEUE(retReturnCode, queueName)                                \
    do {                                                                       \
        if (queueName != nullptr) {                                            \
            retReturnCode.combine(Totem::Queue::Platform::destroy(queueName)); \
            queueName = nullptr;                                               \
        }                                                                      \
    } while (0)
