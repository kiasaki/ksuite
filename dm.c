#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>

#define XSESSION_PATH "/etc/X11/Xsession"
#define XINITRC_PATH ".xinitrc"

static pam_handle_t *pam_h = NULL;

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

static int pam_null_conv(int num_msg, const struct pam_message **msg,
                         struct pam_response **resp, void *appdata_ptr) {
    (void)num_msg; (void)msg; (void)appdata_ptr;
    *resp = NULL;
    return PAM_SUCCESS;
}

static int pam_start_session(const char *username, const char *tty, const char *display) {
    struct pam_conv conv = { pam_null_conv, NULL };
    int ret;

    ret = pam_start("kdm", username, &conv, &pam_h);
    if (ret != PAM_SUCCESS) {
        fprintf(stderr, "pam_start failed: %s\n", pam_strerror(pam_h, ret));
        return -1;
    }

    pam_set_item(pam_h, PAM_TTY, tty);
    pam_set_item(pam_h, PAM_XDISPLAY, display);



    ret = pam_setcred(pam_h, PAM_ESTABLISH_CRED);
    if (ret != PAM_SUCCESS) {
        fprintf(stderr, "pam_setcred failed: %s\n", pam_strerror(pam_h, ret));
        pam_end(pam_h, ret);
        return -1;
    }

    ret = pam_open_session(pam_h, 0);
    if (ret != PAM_SUCCESS) {
        fprintf(stderr, "pam_open_session failed: %s\n", pam_strerror(pam_h, ret));
        pam_setcred(pam_h, PAM_DELETE_CRED);
        pam_end(pam_h, ret);
        return -1;
    }

    return 0;
}

static void pam_close_session_cleanup(void) {
    if (pam_h) {
        pam_close_session(pam_h, 0);
        pam_setcred(pam_h, PAM_DELETE_CRED);
        pam_end(pam_h, PAM_SUCCESS);
        pam_h = NULL;
    }
}

static void setup_env(struct passwd *pw) {
    char **pam_envs = pam_getenvlist(pam_h);
    if (pam_envs) {
        for (char **env = pam_envs; *env; env++) {
            putenv(*env);
        }
    }

    setenv("HOME", pw->pw_dir, 1);
    setenv("USER", pw->pw_name, 1);
    setenv("LOGNAME", pw->pw_name, 1);
    setenv("SHELL", pw->pw_shell, 1);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);

    char xauth[256];
    snprintf(xauth, sizeof(xauth), "%s/.Xauthority", pw->pw_dir);
    setenv("XAUTHORITY", xauth, 1);
}

static int switch_to_user(struct passwd *pw) {
    if (initgroups(pw->pw_name, pw->pw_gid) < 0)
        return -1;
    if (setgid(pw->pw_gid) < 0)
        return -1;
    if (setuid(pw->pw_uid) < 0)
        return -1;
    if (chdir(pw->pw_dir) < 0)
        return -1;
    return 0;
}

static void start_session(struct passwd *pw) {
    char xinitrc[512];
    snprintf(xinitrc, sizeof(xinitrc), "%s/%s", pw->pw_dir, XINITRC_PATH);

    if (access(xinitrc, X_OK) == 0) {
        execl(pw->pw_shell, pw->pw_shell, "-l", "-c", xinitrc, NULL);
    } else if (access(XSESSION_PATH, X_OK) == 0) {
        execl(pw->pw_shell, pw->pw_shell, "-l", "-c", XSESSION_PATH, NULL);
    } else {
        execl(pw->pw_shell, pw->pw_shell, "-l", NULL);
    }
    die("execl failed");
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <username>\n", argv[0]);
        return 1;
    }
    if (getuid() != 0) {
        fprintf(stderr, "Must be run as root\n");
        return 1;
    }
    struct passwd *pw = getpwnam(argv[1]);
    if (!pw) {
        fprintf(stderr, "User '%s' not found\n", argv[1]);
        return 1;
    }

    Display *dpy = XOpenDisplay(NULL);
    char *display_str = ":0";
    if (!dpy) {
        setenv("DISPLAY", display_str, 1);

        pid_t xpid = fork();
        if (xpid == 0) {
            execl("/usr/bin/X", "X", ":0", "-nolisten", "tcp", "vt1", NULL);
            die("Failed to start X server");
        } else if (xpid < 0) {
            die("fork failed");
        }

        sleep(2);
        dpy = XOpenDisplay(display_str);
        if (!dpy)
            die("Cannot open display");
    }

    display_str = DisplayString(dpy);
    setenv("DISPLAY", display_str, 1);

    if (pam_start_session(pw->pw_name, "tty1", display_str) < 0) {
        fprintf(stderr, "Warning: PAM session setup failed, continuing without systemd session\n");
    }

    pid_t pid = fork();
    if (pid == 0) {
        setup_env(pw);
        if (switch_to_user(pw) < 0)
            die("Failed to switch user");
        start_session(pw);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        pam_close_session_cleanup();
        XCloseDisplay(dpy);
    } else {
        die("fork failed");
    }

    return 0;
}
