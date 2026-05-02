#pragma once

#define INTERNAL_DEFAULT_TASK(controllerName, taskName, configName)            \
    auto CONCAT(taskName, Hooks) =                                             \
        TaskController::TaskHooks::bind(this->derived());                      \
    FAIL_IF_ERR_FWD(this->_beginTaskController(),                              \
                    "Failed to begin task controller for %s", controllerName); \
    auto CONCAT(taskName, AddResult) = this->_taskController.addTask(          \
        this->config().configName.name, CONCAT(taskName, Hooks));              \
    FAIL_IF_UNEXPECTED(taskName, CONCAT(taskName, AddResult),                  \
                       CONCAT(taskName, AddResult).error(),                    \
                       "Failed to bind task hooks for %s", controllerName);
#define INTERNAL_DEFAULT_TASK_DEFAULT_TASK(controllerName, configName)         \
    INTERNAL_DEFAULT_TASK(controllerName, task, configName)
#define INTERNAL_DEFAULT_TASK_DEFAULT_CONFIG(controllerName)                   \
    INTERNAL_DEFAULT_TASK(controllerName, task, task)
#define INTERNAL_DEFAULT_TASK_DEFAULT_NAME()                                   \
    INTERNAL_DEFAULT_TASK(name, task, task)

#define DEFAULT_TASK(...)                                                      \
    INTERNAL_GET_MACRO_4(_, __VA_ARGS__ __VA_OPT__(, ) INTERNAL_DEFAULT_TASK,  \
                         INTERNAL_DEFAULT_TASK_DEFAULT_TASK,                   \
                         INTERNAL_DEFAULT_TASK_DEFAULT_CONFIG,                 \
                         INTERNAL_DEFAULT_TASK_DEFAULT_NAME)(__VA_ARGS__)

#define INTERNAL_START_TASK(controllerName, taskName, configName)              \
    FAIL_IF_ERR_FWD(                                                           \
        this->_taskController.startTask(taskName, this->config().configName),  \
        "Failed to start task for %s", controllerName);

#define INTERNAL_START_TASK_DEFAULT_TASK(controllerName, configName)           \
    INTERNAL_START_TASK(controllerName, task, configName)

#define INTERNAL_START_TASK_DEFAULT_CONFIG(controllerName)                     \
    INTERNAL_START_TASK(controllerName, task, task)

#define INTERNAL_START_TASK_DEFAULT_NAME() INTERNAL_START_TASK(name, task, task)

#define START_TASK(...)                                                        \
    INTERNAL_GET_MACRO_4(_, __VA_ARGS__ __VA_OPT__(, ) INTERNAL_START_TASK,    \
                         INTERNAL_START_TASK_DEFAULT_TASK,                     \
                         INTERNAL_START_TASK_DEFAULT_CONFIG,                   \
                         INTERNAL_START_TASK_DEFAULT_NAME)                     \
    (__VA_ARGS__)
