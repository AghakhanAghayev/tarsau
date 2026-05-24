#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_FILES 32
#define MAX_TOTAL_SIZE (200 * 1024 * 1024) // 200 MB

typedef struct {
    char filename[256];
    mode_t mode;
    off_t size;
} FileInfo;

int is_text_file(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return 0;

    int c;
    while ((c = fgetc(fp)) != EOF) {
        if ((unsigned char)c > 127) {
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return 1;
}

void create_directory(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
#ifdef _WIN32
        if (mkdir(path) == -1) {
#else
        if (mkdir(path, 0755) == -1) {
#endif
            perror("mkdir");
        }
    }
}

void archive(int count, char **files, const char *output_name) {
    if (count > MAX_FILES) {
        fprintf(stderr, "Hata: En fazla 32 dosya arşivlenebilir.\n");
        exit(1);
    }

    FileInfo info[MAX_FILES];
    long total_size = 0;
    char header[4096] = "";
    char temp[512];

    for (int i = 0; i < count; i++) {
        struct stat st;
        if (stat(files[i], &st) == -1) {
            perror(files[i]);
            exit(1);
        }

        if (!is_text_file(files[i])) {
            printf("%s giriş dosyasının formatı uyumsuzdur!\n", files[i]);
            exit(0);
        }

        strncpy(info[i].filename, files[i], 255);
        info[i].mode = st.st_mode & 0777;
        info[i].size = st.st_size;
        total_size += info[i].size;

        if (total_size > MAX_TOTAL_SIZE) {
            fprintf(stderr, "Hata: Toplam boyut 200 MB'ı geçemez.\n");
            exit(1);
        }

        sprintf(temp, "|%s,%04o,%ld|", info[i].filename, (unsigned int)info[i].mode, (long)info[i].size);
        strcat(header, temp);
    }

    FILE *out = fopen(output_name, "wb");
    if (!out) {
        perror("Arşiv dosyası oluşturulamadı");
        exit(1);
    }

    fprintf(out, "%010ld", (long)strlen(header));
    fwrite(header, 1, strlen(header), out);

    for (int i = 0; i < count; i++) {
        FILE *in = fopen(files[i], "rb");
        char buffer[8192];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
            fwrite(buffer, 1, n, out);
        }
        fclose(in);
    }

    fclose(out);
    printf("Dosyalar birleştirildi.\n");
}

void extract(const char *archive_name, const char *target_dir) {
    if (strstr(archive_name, ".sau") == NULL) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        exit(1);
    }

    FILE *in = fopen(archive_name, "rb");
    if (!in) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        exit(1);
    }

    char size_buf[11] = {0};
    if (fread(size_buf, 1, 10, in) != 10) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        fclose(in);
        exit(1);
    }

    long header_size = atol(size_buf);
    char *header = malloc(header_size + 1);
    if (fread(header, 1, (size_t)header_size, in) != (size_t)header_size) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        free(header);
        fclose(in);
        exit(1);
    }
    header[header_size] = '\0';

    if (target_dir) {
        create_directory(target_dir);
        if (chdir(target_dir) == -1) {
            perror("Dizin değiştirilemedi");
            exit(1);
        }
    }

    char *ptr = header;
    while ((ptr = strchr(ptr, '|')) != NULL) {
        ptr++; // Skip '|'
        char *end = strchr(ptr, '|');
        if (!end) break;
        *end = '\0';

        char filename[256];
        unsigned int mode;
        long size;
        sscanf(ptr, "%[^,],%o,%ld", filename, &mode, &size);

        FILE *out = fopen(filename, "wb");
        if (!out) {
            perror("Dosya oluşturulamadı");
            exit(1);
        }

        char buffer[8192];
        long remaining = size;
        while (remaining > 0) {
            size_t to_read = ((size_t)remaining > sizeof(buffer)) ? sizeof(buffer) : (size_t)remaining;
            size_t n = fread(buffer, 1, to_read, in);
            fwrite(buffer, 1, n, out);
            remaining -= (long)n;
        }
        fclose(out);
#ifndef _WIN32
        chmod(filename, mode);
#endif

        ptr = end + 1;
    }

    free(header);
    fclose(in);
    printf("Arşiv başarıyla açıldı.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Kullanım:\n  tarsau -b t1 t2 ... -o s1.sau\n  tarsau -a s1.sau [dizin]\n");
        return 1;
    }

    if (strcmp(argv[1], "-b") == 0) {
        char *output_name = "a.sau";
        int file_count = 0;
        char **files = malloc(argc * sizeof(char *));
        if (!files) {
            perror("Bellek ayrilamadi");
            return 1;
        }

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0) {
                if (i + 1 < argc) {
                    output_name = argv[i + 1];
                    i++;
                }
            } else {
                files[file_count++] = argv[i];
            }
        }
        archive(file_count, files, output_name);
        free(files);
    } else if (strcmp(argv[1], "-a") == 0) {
        if (argc < 3) {
            printf("Arşiv dosyası adı belirtilmelidir.\n");
            return 1;
        }
        if (argc > 4) {
            printf("Hata: -a parametresi en fazla 2 parametre almalıdır.\n");
            return 1;
        }
        char *archive_name = argv[2];
        char *target_dir = (argc > 3) ? argv[3] : NULL;
        extract(archive_name, target_dir);
    } else {
        printf("Geçersiz parametre: %s\n", argv[1]);
        return 1;
    }

    return 0;
}
