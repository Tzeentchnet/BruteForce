#include "recomp_exit_hook.h"

#include <stdio.h>
#include <string.h>

static int s_primary_calls;
static int s_replacement_calls;
static int s_first_calls;
static int s_second_calls;
static const char *s_last_why;

static void primary(const char *why)
{
    s_primary_calls++;
    s_last_why = why;
}

static void replacement(const char *why)
{
    s_replacement_calls++;
    s_last_why = why;
}

static void first(const char *why)
{
    s_first_calls++;
    s_last_why = why;
}

static void second(const char *why)
{
    s_second_calls++;
    s_last_why = why;
}

static int check(int condition, const char *what)
{
    printf("%-58s %s\n", what, condition ? "PASS" : "FAIL");
    return condition ? 0 : 1;
}

int main(void)
{
    int failures = 0;

    failures += check(!recomp_exit_hook_add(NULL), "null additional hook is rejected");
    recomp_exit_hook_set(primary);
    failures += check(recomp_exit_hook_add(first), "first additional hook registers");
    failures += check(recomp_exit_hook_add(second), "second additional hook registers");
    failures += check(recomp_exit_hook_add(first), "duplicate registration is idempotent");

    recomp_exit_hook_run("firmware");
    failures += check(s_primary_calls == 1 && s_first_calls == 1 && s_second_calls == 1,
                      "primary and independent hooks run once");
    failures += check(strcmp(s_last_why, "firmware") == 0, "exit reason reaches every hook");

    recomp_exit_hook_set(replacement);
    recomp_exit_hook_run("shutdown");
    failures += check(s_primary_calls == 1 && s_replacement_calls == 1,
                      "setting the primary hook replaces only the primary");
    failures += check(s_first_calls == 2 && s_second_calls == 2,
                      "independent hooks survive primary replacement");
    failures += check(strcmp(s_last_why, "shutdown") == 0, "replacement run has new reason");

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
