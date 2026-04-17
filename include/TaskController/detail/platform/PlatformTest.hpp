struct TestPlatform {
    using CreateTaskHook = ReturnCode (*)(CorePreference);

    inline static CreateTaskHook create_task_hook = nullptr;
    inline static int create_task_calls = 0;
    inline static CorePreference last_core = CorePreference::any();

    static void reset() {
        create_task_hook = nullptr;
        create_task_calls = 0;
        last_core = CorePreference::any();
    }

    static ReturnCode default_create_task(CorePreference) {
        return ReturnCode::Ok;
    }

    static ReturnCode create_task(CorePreference core) {
        ++create_task_calls;
        last_core = core;
        auto *fn = create_task_hook ? create_task_hook : default_create_task;
        return fn(core);
    }
};
