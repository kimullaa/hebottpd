#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "service.h"
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#include <syslog.h>

#define MAX_BUFFER_SIZE 1000

#define TIME_BUF_SIZE 64
#define SERVER_NAME "hebottpd"
#define SERVER_VERSION "1.0"
#define HTTP_MINOR_VERSION 0

struct HTTPHeaderField {
  char *name;
  char *value;
  struct HTTPHeaderField *next; 
} typedef HTTPHeaderField;

struct {
  int protocol_minor_version;
  char *method;
  char *path;
  HTTPHeaderField *header;
  char *body;
  long length;
} typedef HTTPRequest;

struct {
  char *path;
  long size;
  int ok;
} typedef FileInfo;

static void free_request(HTTPRequest *req);
static HTTPRequest* read_request(FILE *in);
static HTTPRequest* read_http_request(FILE *in);
static HTTPHeaderField* read_header_field(FILE *in);
static void responsd_to(HTTPRequest *req, FILE *out, char *docroot);
static FileInfo* read_file_info(char *path, char *docroot);
static char* build_path(char *docroot, char *path);
static void write_to(FILE *out, HTTPRequest *req, char *docroot);
static void not_supported(FILE *out, HTTPRequest *req, char *docroot);
static void write_body(FILE *fp, FILE *out);
static void write_headers(HTTPRequest *req, FILE *out, int status);
static void free_fileinfo(FileInfo *fileInfo);
static char *filename_not_supported = "not-supported.html";
static char *filename_not_found = "not-found.html";

static void free_request(HTTPRequest *req) {
  if (req == NULL) return;

  HTTPHeaderField *head, *tmp;
  head = req->header;
  while (head != NULL) {
    tmp = head;
    head = head->next;
    free(tmp->name);
    free(tmp->value);
    free(tmp);
  }

  free(req->method);
  free(req->path);
  free(req->body);
  free(req);
}

static HTTPRequest* read_http_request(FILE *in) {
  char buf[MAX_BUFFER_SIZE] = { '0' };

  if (fgets(buf, sizeof(buf), in) == NULL) {
    perror("read_http_request");
    exit(1);
  }

  HTTPRequest *req;
  req = xalloc(sizeof(HTTPRequest));

  char *p;
  if ((p = strchr(buf, ' ')) == NULL) {
    perror("parse error");
    exit(1);
  }
  *p++ = '\0';

  req->method = xalloc(p - buf);
  strcpy(req->method, buf);

  char *q;
  if ((q = strchr(p, ' ')) == NULL) {
    perror("parse errorxxx");
    exit(1);
  }
  *q++ = '\0';
  req->path = xalloc(q - p);
  strcpy(req->path, p);

  if (strncmp(q, "HTTP/1.", strlen("HTTP/1.")) == 0) {
    p += strlen("HTTP/1.");
    req->protocol_minor_version = atoi(p);
  } else {
    log_exit("NOT SUPPORTED");
  }
  return req;
}

static HTTPHeaderField* read_header_field(FILE *in) {
  char buf[MAX_BUFFER_SIZE] = { '0' };

  if (fgets(buf, sizeof(buf), in) == NULL) {
    perror("read_header_field");
    exit(1);
  }


  if (buf[0] == '\n' || strcmp(buf, "\r\n") == 0) {
    return NULL;
  }

  char *p;
  p = strchr(buf, ':');
  if (!p) {
    log_exit("parse error");
  }
  *p++ = '\0';

  HTTPHeaderField *h;
  h = xalloc(sizeof(HTTPHeaderField));
  h->name = xalloc(p - buf);
  strcpy(h->name, buf);

  p += strspn(p, " \t");
  h->value = xalloc(strlen(p) + 1);
  strcpy(h->value, p);

  return h;
}

static HTTPRequest* read_request(FILE *in) {
  HTTPRequest *req;
  req = read_http_request(in);

  HTTPHeaderField *h;
  while ((h = read_header_field(in)) != NULL) {
    h->next = req->header;
    req->header = h;
  }

  return req;
}

static void responsd_to(HTTPRequest *req, FILE *out, char *docroot) {
  if (strcmp(req->method, "GET") == 0) {
    write_to(out, req, docroot);
  } else if (strcmp(req->method, "POST") == 0) {
    not_supported(out, req, docroot);
  }
}

static void write_to(FILE *out, HTTPRequest *req, char *docroot) {
  FileInfo *fileInfo;
  fileInfo = read_file_info(req->path, docroot);


  FILE *fp;
  if (!fileInfo->ok) {
    char *path;
    path = malloc(strlen(docroot) + strlen(filename_not_found) + 2);
    sprintf(path, "%s/%s",docroot, filename_not_found);
    if ((fp = fopen(path, "r")) == NULL) {
      log_exit("can't open file %s", fileInfo->path);
    }
    write_headers(req, out, 404);
    write_body(fp, out);
  } else {
    if ((fp = fopen(fileInfo->path, "r")) == NULL) {
      log_exit("can't open file %s", fileInfo->path);
    }
    write_headers(req, out, 200);
    write_body(fp, out);
  }

  if (fclose(fp) == EOF || fclose(out) == EOF) {
    log_exit("can't close");
  }
  
  free_fileinfo(fileInfo);
}

static void not_supported(FILE *out, HTTPRequest *req, char *docroot) {
  char *path;
  path = xalloc(strlen(docroot) + strlen(filename_not_supported) + 2);
  sprintf(path, "%s/%s", docroot, filename_not_supported);

  FILE *fp;
  if ((fp = fopen(path, "r")) == NULL) {
    log_exit("can't open file %s", path);
  }

  write_headers(req, out, 405);
  write_body(fp, out);

  if (fclose(fp) == EOF || fclose(out) == EOF) {
    log_exit("can't close");
  }

}
static void write_headers(HTTPRequest *req, FILE *out, int status) {

  time_t t;
  struct tm *tm;
  char buf[TIME_BUF_SIZE];

  t = time(NULL);
  tm = gmtime(&t);
  if (!tm) log_exit("gmtime() failed: %s", strerror(errno));
  strftime(buf, TIME_BUF_SIZE, "%a, %d %b %Y %H:%M:%S GMT", tm);

  fprintf(out, "HTTP/1.%d %d\r\n", HTTP_MINOR_VERSION, status);
  fprintf(out, "Date: %s\r\n", buf);
  fprintf(out, "Server: %s/%s\r\n", SERVER_NAME, SERVER_VERSION);
  fprintf(out, "Connection: close\r\n");

  fflush(out);
}

static void write_body(FILE *fp, FILE *out) {
  fprintf(out,"\r\n");
  int n;
  while ((n = fgetc(fp)) != EOF) {
    if (fputc(n, out) == EOF) {
      log_exit("can't write");
    }
  }

  fflush(out);
}

static FileInfo* read_file_info(char *path, char *docroot) {
  FileInfo *fileInfo;
  fileInfo = xalloc(sizeof(FileInfo));

  fileInfo->path = build_path(docroot, path);

  struct stat info;
  if (lstat(fileInfo->path, &info) == -1) {
    fileInfo->ok = 0;
    return fileInfo;
  }

  // 通常ファイル以外はダメ
  if (!S_ISREG(info.st_mode)) {
    fileInfo->ok = 0;
    return fileInfo;
  }

  fileInfo->size = info.st_blksize;
  fileInfo->ok =1;
  return fileInfo;
}

static void free_fileinfo(FileInfo *fileInfo) {
  free(fileInfo->path);
  free(fileInfo);
}

static char* build_path(char *docroot, char *path) {
  char *fullpath;

  fullpath = xalloc(strlen(docroot) + strlen(path) + 4);
  sprintf(fullpath, "./%s/%s", docroot, path);
  return fullpath;
}

void service(FILE *in, FILE *out, char *docroot) {
  HTTPRequest *req;
  req = read_request(in);
  responsd_to(req, out, docroot);
  free_request(req);
}
