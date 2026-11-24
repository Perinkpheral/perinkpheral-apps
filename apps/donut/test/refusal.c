// SPDX-License-Identifier: MIT
#include <assert.h>
#include "../donut.c"
#include "standin.h"

int main(void)
{
    const int errors[] = { HOST_ERR_NO_SPACE, HOST_ERR_FAILED };
    for (unsigned i = 0; i < sizeof errors / sizeof errors[0]; ++i) {
        standin_reset();
        standin()->is_session_writable = true;
        app_on_load();
        app_on_focus();
        app_on_button(BTN_DOWN_PRESSED);
        app_on_button(BTN_OK_PRESSED);
        standin_refuse_session(errors[i]);
        app_on_blur();
        assert(standin_refused_sessions() == 1);
        assert(standin_session_failure_reported());
        standin_refuse_session(HOST_OK);
        int logs = standin()->logs;
        app_on_blur();
        assert(standin()->session_length > 0 && standin()->logs == logs);
    }
    return 0;
}
