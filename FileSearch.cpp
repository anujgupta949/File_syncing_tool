#include <Shlwapi.h>
#include <corecrt.h>
#include <cstdlib>
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <cstdio>
#include <winnt.h>
#include <winscard.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include "cJSON.h"


#define uint64 uint64_t
#define BufSize MAX_PATH
using namespace std;

struct FileInfo{
  char RelativeFilePath[BufSize];
  uint64 FileSize;
  uint64 ModifiedTime;
  uint64 CreationTime;
};

struct snapshot {
  struct FileInfo *File;
  int Count;
  int Capacity;
  FILETIME SnapTime;
};


void PrintFiles(struct snapshot *Snap,
                const char *CurrentFolder, const char* RootFolder) {

  char SearchPath[MAX_PATH];
  snprintf(SearchPath, MAX_PATH, "%s\\*", CurrentFolder);

  WIN32_FIND_DATA FileTree;
  HANDLE FileHandle = FindFirstFile(SearchPath, &FileTree);

    if (FileHandle == INVALID_HANDLE_VALUE)
    {
      printf("Directory not found\n");
      return;
    }

  do
    {
	  if (strcmp(FileTree.cFileName, ".") == 0 || strcmp(FileTree.cFileName, "..") == 0)
	    continue;
      if (FileTree.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      {

      char NextFolder[MAX_PATH];

      snprintf(NextFolder, MAX_PATH, "%s\\%s", CurrentFolder,
               FileTree.cFileName);

      PrintFiles(Snap, NextFolder, RootFolder);

       }

      else

      {
        int *Counter = &Snap->Count;
        int *Capacity = &Snap->Capacity;
        char FullFilePath[MAX_PATH];
        snprintf(FullFilePath, MAX_PATH, "%s\\%s", CurrentFolder,
                 FileTree.cFileName);
        /*
          char RelativePath[MAX_PATH];

        snprintf(RelativePath, MAX_PATH, "%s",  FullFilePath + strlen(RootFolder)+1);

*/
        char RelativePath[MAX_PATH];
        bool success = PathRelativePathToA(RelativePath, RootFolder,
                                           FILE_ATTRIBUTE_DIRECTORY,
                                           FullFilePath, FILE_ATTRIBUTE_NORMAL);


        LARGE_INTEGER FileSize;
        FileSize.LowPart = FileTree.nFileSizeLow;
        FileSize.HighPart = FileTree.nFileSizeHigh;


        //	  SYSTEMTIME LocalTime;
        //FileTimeToSystemTime(&Timestamp, &LocalTime);
        ULARGE_INTEGER mTime;
        mTime.LowPart = FileTree.ftLastWriteTime.dwLowDateTime;
        mTime.HighPart = FileTree.ftLastWriteTime.dwHighDateTime;
        //	  printf("| Last Modified time: %u:%u\n", LocalTime.wHour,
        //LocalTime.wMinute);

        ULARGE_INTEGER cTime;
        cTime.HighPart = FileTree.ftCreationTime.dwHighDateTime;
        cTime.LowPart = FileTree.ftCreationTime.dwLowDateTime;

        if (*Counter >= *Capacity) {
          int NewCapacity = *Capacity * 2;
          struct FileInfo *Temp =  (struct FileInfo *)realloc(Snap->File , NewCapacity * (sizeof(struct FileInfo)));



        if (Temp != NULL) {
          Snap->File = Temp;
          *Capacity = NewCapacity;
       }
        }
        if (success) {
        strncpy_s((Snap->File)[*Counter].RelativeFilePath,  RelativePath+2, MAX_PATH - 1) ;
        } else

          strncpy_s((Snap->File)[*Counter].RelativeFilePath,  FullFilePath , MAX_PATH - 1) ;


        (Snap->File)[*Counter].RelativeFilePath[MAX_PATH - 1] = '\0';
        (Snap->File)[*Counter].FileSize = FileSize.QuadPart;
        (Snap->File)[*Counter].ModifiedTime = mTime.QuadPart;
        (Snap->File)[*Counter].CreationTime = cTime.QuadPart;
        printf("Stored [%d]: %s\n\n\n" , *Counter, FullFilePath);
        (*Counter)++;
      }

    }
  while(FindNextFile(FileHandle, &FileTree) != 0);
}


int Cmp(const void *a,const  void *b)
{

 struct FileInfo *a1 = (struct FileInfo *)a;
 struct FileInfo *a2 = (struct FileInfo *)b;


 char *name1 = strrchr(a1->RelativeFilePath, '\\');
 char *name2 = strrchr(a2->RelativeFilePath, '\\');

 char *FinalName1 = name1 ? name1+1 : a1->RelativeFilePath;
 char *FinalName2 = name2 ? name2+1 : a2->RelativeFilePath;
 return strcmp(FinalName1, FinalName2);
}




int main ()
{
  struct snapshot Snap1;
  Snap1.Count = 0;
  Snap1.Capacity = 10;
 Snap1.File = (struct FileInfo *)malloc(Snap1.Capacity*sizeof(struct FileInfo));

 struct snapshot Snap2;
 Snap2.Count = 0;
 Snap2.Capacity = 10;
 Snap2.File = (struct FileInfo *)malloc(Snap2.Capacity*sizeof(struct FileInfo));

    PrintFiles(&Snap1, "c:\\dev\\2026\\minor_project", "c:\\dev\\2026\\minor_project");
    PrintFiles(&Snap2, "c:\\dev\\2026\\copytest", "c:\\dev\\2026\\copytest");


    qsort(Snap1.File, Snap1.Count, sizeof(struct FileInfo), Cmp);
    qsort(Snap2.File, Snap2.Count , sizeof(struct FileInfo), Cmp);

    cJSON *array = cJSON_CreateArray();
    for (int i = 0; i < Snap1.Count; i++) {
      cJSON *Obj = cJSON_CreateObject();


      cJSON_AddStringToObject(Obj, "path", Snap1.File[i].RelativeFilePath);
      cJSON_AddNumberToObject(Obj, "size", Snap1.File[i].FileSize);

      cJSON_AddNumberToObject(Obj, "modified time", Snap1.File[i].ModifiedTime);
      cJSON_AddNumberToObject(Obj, "creation time", Snap1.File[i].CreationTime);

      cJSON_AddItemToArray(array, Obj );
    }

    char *JSON_String = cJSON_PrintUnformatted(array);
    cJSON_Delete(array);


    FILE *fp;
    errno_t error;
    error = fopen_s(&fp , "Snapshot.bin", "wb");
    if (!error) {
      fwrite(JSON_String, 1, strlen(JSON_String),  fp);
    }
    fclose(fp);


    free(JSON_String);


    FILE *print;
    errno_t outerror;

    struct FileInfo Final[Snap1.Count + Snap2.Count];
    int index = 0;

    int i = 0, j = 0;

    while (i < Snap1.Count && j < Snap2.Count) {

      char *Name1 = strrchr(Snap1.File[i].RelativeFilePath, '\\');
      char *Name2 = strrchr(Snap2.File[j].RelativeFilePath, '\\');
      int cmp = strcmp(Name1 ? Name1+1 : Snap1.File[i].RelativeFilePath , Name2 ? Name2+1 : Snap2.File[j].RelativeFilePath);

      if (cmp == 0) {
        if (Snap1.File[i].ModifiedTime > Snap2.File[j].ModifiedTime) {
        Final[index++] = Snap1.File[i];
        }
        else if(Snap1.File[i].ModifiedTime == Snap2.File[j].ModifiedTime) {
         Final[index++] = Snap1.File[i];
        }
        else {
         Final[index++] = Snap2.File[j];
        }
        i++;
        j++;
      }

      else if (cmp < 0) {
        Final[index++] = Snap1.File[i];
        i++;
      }

      else {
        Final[index++] = Snap2.File[j];
        j++;
      }
    }

    while (i < Snap1.Count) {
      Final[index++] = Snap1.File[i++];
    }
    while (j < Snap2.Count) {
      Final[index++] = Snap2.File[j++];
    }
/*
    for (int i = 0; i < Snap1.Count; ++i) {
      printf("RelativeFilePath: %s\n", Snap1.File[i].RelativeFilePath);
    }
    printf("%d\n\n\n", Snap1.Count);

    for (int i = 0; i < Snap2.Count; ++i) {
      printf("RelativeFilePath: %s\n", Snap2.File[i].RelativeFilePath);
    }
    printf("%d\n\n\n", Snap2.Count);

    for (int i = 0; i < index; ++i) {
      printf("RelativeFilePath: %s\n", Final[i].RelativeFilePath);
    }
    printf("%d\n\n\n", index);

*/
  return 0;
}
