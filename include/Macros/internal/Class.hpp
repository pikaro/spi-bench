#pragma once

#define DELETE_COPY_CONSTRUCTOR(Class) Class(const Class &) = delete;
#define DELETE_COPY_ASSIGNMENT(Class) Class &operator=(const Class &) = delete;
#define DELETE_MOVE_CONSTRUCTOR(Class) Class(Class &&) = delete;
#define DELETE_MOVE_ASSIGNMENT(Class) Class &operator=(Class &&) = delete;

#define DELETE_COPY(Class)                                                     \
    DELETE_COPY_CONSTRUCTOR(Class)                                             \
    DELETE_COPY_ASSIGNMENT(Class)

#define DELETE_MOVE(Class)                                                     \
    DELETE_MOVE_CONSTRUCTOR(Class)                                             \
    DELETE_MOVE_ASSIGNMENT(Class)
