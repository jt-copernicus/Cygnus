/*
 * cygnus-mount
 * Automounter program for Cygnus WM.
 *
 * by Jonathan Torres
 * 
 * This program is free software: you can redistribute it and/or modify it under the terms of the 
 * GNU General Public License as published by the Free Software Foundation, either version 3 of 
 * the License, or (at your option) any later version.
 * 
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>

void mount_device(const char *node) {
    if (fork() == 0) {
        printf("Cygnus Mount: mounting %s\n", node);
        execlp("udisksctl", "udisksctl", "mount", "-b", node, (char *)NULL);
        exit(0);
    }
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("Cygnus Automounter started.\n");
    
    FILE *fp = popen("udisksctl monitor", "r");
    if (!fp) {
        perror("popen");
        return 1;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "Added") && strstr(line, "/block_devices/")) {
            char *p = strstr(line, "/block_devices/");
            p += 15;
            char node[64];
            int i = 0;
            while (p[i] && !isspace(p[i]) && i < 63) {
                node[i] = p[i];
                i++;
            }
            node[i] = '\0';
            
            if ((strncmp(node, "sd", 2) == 0 || strncmp(node, "nvme", 4) == 0) && isdigit(node[strlen(node)-1])) {
                char dev_path[128];
                snprintf(dev_path, sizeof(dev_path), "/dev/%s", node);
                mount_device(dev_path);
            }
        }
    }
    pclose(fp);
    return 0;
}
