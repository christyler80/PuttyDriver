#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "putty.h"
#include "terminal.h"

#define DECIMAL 10

#define IDM_CLRSB 0x60L
#define IDM_COPYALL 0x170L

#define MAX_BUFFER_SIZE 2048
#define MAX_STRING_LENGTH 256

#define MAX_MESSAGE_LENGTH 4096

#define MAX_KEYCODES_SIZE 1024
#define MAX_FILENAME_SIZE 1024

#define MAX_SCREEN_COLS MAX_STRING_LENGTH
#define MAX_SCREEN_ROWS MAX_BUFFER_SIZE

#define MAX_RAWDATA_LEN 32768

#define vTerm_Command_Max_Wait 15
#define vTerm_Command_TimeOut 30

#define vTerm_KeyName 0
#define vTerm_KeyANSI 1

#define vTerm_KeyCode_Elements 3

#define vTerm_Sessions_Max 8
#define vTerm_Session_Offset 14

#define vTerm_Command_Elements 12
#define vTerm_Commands_Max 250

#define vTerm_Command_Seq_pos 0
#define vTerm_Expected_Screen_Identifier_pos 1
#define vTerm_Expected_Screen_Identifier_At_pos 2
#define vTerm_Screen_Capture_OnOff_pos 3
#define vTerm_Expected_Command_Prompt_pos 4
#define vTerm_Expected_Command_Prompt_At_pos 5
#define vTerm_Expected_Input_Cursor_At_pos 6
#define vTerm_Command_Send_pos 7
#define vTerm_Command_Input_Hidden_pos 8
#define vTerm_Command_Submit_Key_pos 9
#define vTerm_Command_Send_Pause_pos 10
#define vTerm_DBRecord_Script_Cmd_ID_pos 11
#define vTerm_DBRecord_Session_Cmd_ID_pos 12
#define vTerm_Screen_Identifier_At_pos 13
#define vTerm_Command_Prompt_At_pos 14
#define vTerm_Command_Sent_Cursor_At_pos 15
#define vTerm_Command_Prompt_OK_pos 16
#define vTerm_Command_Processed_pos 17
#define vTerm_Command_Processed_Submit_Key_pos 18
#define vTerm_Current_Cursor_pos 19
#define vTerm_Command_Processed_ASCII_pos 20
#define vTerm_DBRecord_Scrn_ID_pos 21
#define vTerm_Output_ASCII_pos 22
#define vTerm_Output_pos 23
#define vTerm_Screen_Command_Seq_pos 24
#define vTerm_Screen_pos 25

char DBDelimiter;

char String_Array[MAX_BUFFER_SIZE][MAX_STRING_LENGTH];

int CaptureScreensData;

int RecordForScripting;

int SessionsKeyPressSync;

typedef struct {
    int X;
    int Y;
} CursorPos;

typedef struct {
    bool Command_Auto;
    long Command_Script_DB_ID;
    long Command_Session_DB_ID;
    int Command_Seq;
    int Command_Seq_Max;
    char Command_Input_Hidden[MAX_STRING_LENGTH];
    bool Command_Mismatch;
    char Command_Mismatch_Pos_Actual[MAX_STRING_LENGTH];
    char Command_Mismatch_Pos_Expected[MAX_STRING_LENGTH];
    char Command_Mismatch_Type[MAX_STRING_LENGTH];
    char Command_Processed[MAX_BUFFER_SIZE];
    int Command_Processed_Len;
    char Command_Processed_ASCII[MAX_BUFFER_SIZE];
    bool Command_Processed_Logging;
    char Command_Processed_Manual_Input[MAX_BUFFER_SIZE];
    char Command_Processed_Submit_Key[MAX_STRING_LENGTH];
    int Command_Processed_Submit_Key_Len;
    bool Command_Processing;
    bool Command_Processing_Finished;
    bool Command_Processing_Started;
    char Command_Prompt[MAX_STRING_LENGTH];
    int Command_Prompt_Len;
    char Command_Prompt_Pos[MAX_STRING_LENGTH];
    char Command_Prompt_Expected[MAX_STRING_LENGTH];
    int Command_Prompt_Expected_Len;
    char Command_Prompt_Expected_Pos[MAX_STRING_LENGTH];
    int Command_Prompt_Expected_Pos_X;
    int Command_Prompt_Expected_Pos_Y;
    char Command_Prompt_OK[MAX_STRING_LENGTH];
    char Command_Current_Cursor_Pos[MAX_STRING_LENGTH];
    int Command_Current_Seq;
    char Command_Screen_Identifier[MAX_STRING_LENGTH];
    int Command_Screen_Identifier_Len;
    char Command_Screen_Identifier_Pos[MAX_STRING_LENGTH];
    int Command_Screen_Identifier_Pos_X;
    int Command_Screen_Identifier_Pos_Y;
    time_t Command_Send_At;
    char Command_Send[MAX_STRING_LENGTH];
    int Command_Send_Len;
    int Command_Send_Buffer_Len;
    char Command_Send_Expected_Cursor[MAX_STRING_LENGTH];
    int Command_Send_Expected_Cursor_X;
    int Command_Send_Expected_Cursor_Y;
    int Command_Send_Pause;
    int Command_Send_Pos;
    char Command_Sent[MAX_STRING_LENGTH];
    int Command_Sent_Len;
    char Command_Sent_Cursor_Pos[MAX_STRING_LENGTH];
    bool Command_Submit_Key;
    bool Command_Wait;
    time_t Command_Wait_Until;
    char Commands_Input[MAX_RAWDATA_LEN];
    char Commands_Processed[MAX_RAWDATA_LEN];
    int Controller_Updated_Seq;
    long Hwnd;
    time_t Message_At;
    long Pid;
    int Row;
    time_t Row_Updated_At;
    char Screen[MAX_SCREEN_ROWS * MAX_SCREEN_COLS];
    char Screen_Array[MAX_SCREEN_ROWS][MAX_SCREEN_COLS];
    int Screen_Array_Rows;
    char Screen_Capture[MAX_STRING_LENGTH];
    int Screen_Capture_Command_Seq_From;
    int Screen_Capture_Offset;
    bool Screen_Capture_Pending;
    int Screen_Capture_RGB;
    int Screen_Columns_X;
    CursorPos Screen_Cursor;
    int Screen_Cursor_Prev_X;
    int Screen_Cursor_Prev_Y;
    char Screen_Identifier[MAX_STRING_LENGTH];
    char Screen_Identifier_Pos[MAX_STRING_LENGTH];
    int Screen_Len;
    int Screen_Lines;
    bool Screen_Get;
    char Screen_New[MAX_RAWDATA_LEN];
    int Screen_New_Len;
    int Screen_New_Rows;
    int Screen_Ptr;
    char Screen_Raw[MAX_RAWDATA_LEN];
    char Screen_Raw_ASCII[MAX_RAWDATA_LEN];
    long Screen_Command_Session_DB_ID_From;
    long Screen_Command_Session_DB_ID_To;
    int Screen_Command_Seq_From;
    int Screen_Command_Seq_To;
    int Screen_Requested_Seq;
    int Screen_Rows_Y;
    int Session_ID;
    char Session_Name[MAX_STRING_LENGTH];
    char SessionTimeStamp[MAX_STRING_LENGTH];
    char Submit_Key[MAX_STRING_LENGTH];
    int Submit_Key_Len;
    char Submit_Key_ANSI[MAX_STRING_LENGTH];
    char Submit_Key_Send[MAX_STRING_LENGTH];
} vTerminal;

vTerminal vTerm;

char vTermSessionID[MAX_STRING_LENGTH];

char vTermKeyCodes[MAX_KEYCODES_SIZE][vTerm_KeyANSI + 1][MAX_STRING_LENGTH];

char vTermCommands[301][13][MAX_STRING_LENGTH];

int vTermCommands_File;
int vTermLog_File;
int vTermScreens_File;

int vTermScreenText_Pos_X;
int vTermScreenText_Pos_Y;

int vTerm_Stop;

FILE *vTermLog_Stream;
FILE *vTermCapture_Stream;
FILE* vTermCaptureInputs_Stream;

int datetest()
{

    time_t curr_time;

    struct tm* curr_tm;

    char date_string[MAX_STRING_LENGTH];
    char time_string[MAX_STRING_LENGTH];

    time(&curr_time);

    curr_tm = localtime(&curr_time);

    strftime(date_string, 26, "%Y-%m-%d %H:%M:%S", curr_tm);
    strftime(date_string, 50, "Today is %B %d, %Y", curr_tm);
    strftime(time_string, 50, "Current time is %T", curr_tm);

    printf("%s\n", date_string);
    printf("%s\n", time_string);

    time_t yourdate;

    time(&yourdate);

    printf("%ld\n", yourdate);

    return 0;
}

static uint64_t GetAvailableStackSpace()
{
    volatile uint8_t var;

    MEMORY_BASIC_INFORMATION mbi;

    auto virtualQuerySuccess = VirtualQuery((LPCVOID)&var, &mbi, sizeof(mbi));

    if (!virtualQuerySuccess)
    {
        return 0;
    }

    return &var - mbi.AllocationBase;
}

bool file_exists(const char* filename) {

    struct stat buffer;

    return (stat(filename, &buffer) == 0);
}

int instr(char* base, char* str, int startIndex) {

    int result;
    int baselen = strlen(base);

    // str should not longer than base.
    if (strlen(str) > baselen || startIndex > baselen) {
        result = -1;
    }
    else {

        if (startIndex < 0) {
            startIndex = 0;
        }

        char* pos = strstr(base + startIndex, str);

        if (pos == NULL) {
            result = -1;
        }
        else {
            result = pos - base;
        }
    }
    return result;
}

int instrrev(const char* base, const char* str) {

    int result;

    // str should not be longer than base.
    if (strlen(str) > strlen(base)) {
        result = -1;
    }
    else {

        int start = 0;

        int endinit = strlen(base) - strlen(str);

        int end = endinit;
        int endtmp = endinit;

        while (start != end) {

            start = instr(base, str, start);
            end = instr(base, str, end);

            // Not found from start.
            if (start == -1) {
                end = -1; // then break;
            }
            else if (end == -1) {
                /* Found from start
                   but not found from end
                   move end to middle. */
                if (endtmp == (start + 1)) {
                    end = start; // then break;
                }
                else {
                    end = endtmp - (endtmp - start) / 2;
                    if (end <= start) {
                        end = start + 1;
                    }
                    endtmp = end;
                }
            }
            else {
                /* Found from both start and end
                   move start to end and
                   move end to base - strlen(str). */
                start = end;
                end = endinit;
            }
        }
        result = start;
    }
    return result;
}

char* inttostr(int num)
{
    char str[MAX_STRING_LENGTH];

    itoa(num, str, DECIMAL);

    return str;
}

bool isnull(void* var) {

    if (var == NULL) {
        return true;
    }
    else if (var == (void*)-1) { // Assuming -1 represents DBNull.
        return true;
    }
    else if (strcmp(var, "") == 0) {
        return true;
    }
    else {
        return false;
    }
}

void* ifnull(void* var, void* value) {

    if (isnull(var) == true) {
        return value;
    }
    else {
        return var;
    }
}

bool isnumeric(const char* str) {

    size_t index = 0;

    int has_digits = 0, has_dot = 0;

    char cc;

    if (!str) {
        return 0;
    }

    if (*str == '+' || *str == '-') {
        index++;
    }

    while ((cc = str[index++]) != '\0') {

        if (cc == '.') {
            if (has_dot++)
                return 0;
        }
        else {

            if (cc >= '0' && cc <= '9') {
                has_digits = 1;
            }
            else {
                return 0;
            }
        }
    }

    return has_digits;
}

void append_char(char* str, char ch, int max_len) {

    int len = strlen(str) + 1;

    if (len < max_len) {

        str[len - 1] = ch;

        str[len] = '\0';

    }
    else {

        MessageBox(NULL, dupprintf("Fatal Error : Max string array size '%d' exceeded - exiting program.", max_len), "Putty Driver", MB_ICONERROR | MB_OK);

        exit(EXIT_FAILURE);
    }
}

void append_string(char* str, char* append, int max_len) {

    int len = strlen(str) + strlen(append);

    if (len < max_len) {

        strcat(str, append);

        str[len] = '\0';

    } else {

        MessageBox(NULL, dupprintf("Fatal Error : Max string array size '%d' exceeded - exiting program.", max_len), "Putty Driver", MB_ICONERROR | MB_OK);

        exit(EXIT_FAILURE);
    }
}

char* string_replacechar(char* str, const char* old, const char* new) {

    int len = strlen(str);

    for (int i = 0; i < len; i++) {

        if (str[i] == old) {
            str[i] = new;
        }
    }

    return str;
}

int string_split(const char* input, const char delimiter, const int array_size, const int field_size) {

    int i = 0;
    int j = 0;
    
    int ctr;

    ctr = 0;

    memset(String_Array, 0, (MAX_BUFFER_SIZE * MAX_STRING_LENGTH));

    for (i = 0; i < strlen(input); i++) {

        if (input[i] == delimiter || input[i] == '\0') {

            String_Array[ctr][j] = '\0';

            if (j > 0 && input[i] == '\n') {
                if (String_Array[ctr][j - 1] == '\r') String_Array[ctr][j - 1] = '\0';
            }

            if (ctr < array_size) {
                ctr++;
            }
            else {

                MessageBox(NULL, dupprintf("Fatal Error : Max string array size '%d' exceeded - exiting program.", array_size), "Putty Driver", MB_ICONERROR | MB_OK);

                exit(EXIT_FAILURE);

            }

            j = 0;

        }
        else {
            
            if (isascii(input[i]) == true) {
                String_Array[ctr][j] = input[i];
            }
            else {
                String_Array[ctr][j] = ' ';
            }

            if (j < field_size) {
                j++;
            } else {

                MessageBox(NULL, dupprintf("Fatal Error : Max string array size '%d' exceeded - exiting program.", field_size), "Putty Driver", MB_ICONERROR | MB_OK);

                exit(EXIT_FAILURE);
            }
        }
    }

    return ctr;
}

char* ltrim(char* s) {

    if (isnull(s)) return s;

    while (isspace(*s)) s++;

    return s;
}

char* rtrim(char* s) {

    if (isnull(s)) return s;

    char* back = s + strlen(s);

    while (isspace(*--back));

    *(back + 1) = '\0';

    return s;
}

char* trim(char* s) {

    if (isnull(s)) return s;

    return rtrim(ltrim(s));
}

void vTermSessionTimeStamp() {

    time_t curr_time;

    struct tm* curr_tm;

    time(&curr_time);

    curr_tm = localtime(&curr_time);

    strftime(vTerm.SessionTimeStamp, sizeof(vTerm.SessionTimeStamp), "%y%m%d_%H%M%S", curr_tm);

}

char* vTermSetFileName(char* subfolder, char* filename, char* filesuffix, bool timestamp, bool checkexists) {

    char basename[MAX_FILENAME_SIZE];
    char cwdpath[MAX_FILENAME_SIZE];
    char filepath[MAX_FILENAME_SIZE];
    char folder[MAX_FILENAME_SIZE];

    //char datetime[MAX_STRING_LENGTH];

    int pos = -1;

    struct stat buffer;

    memset(basename, 0, sizeof(basename));

    if (strlen(filename) > 0) {
        strcpy(basename, filename);
    }
    else {

        if (vterm_sessionid > 0) {
            sprintf(basename, "%s_%d", vterm_hostname, vterm_sessionid);
        }
        else {
            sprintf(basename, "%s", vterm_hostname);
        }
    }

    stat(basename, &buffer);

    pos = instrrev(basename, "\\");

    if (pos < 0) {
        pos = instrrev(basename, "//");
    }

    if (pos >= 0) {

        stat(basename, &buffer);

        if ((checkexists == true) && ((buffer.st_mode & S_IFMT) == S_IFREG)) {

            MessageBox(NULL, dupprintf("Fatal Error : Log file '%s' already exists #1 - exiting program.", basename), "Putty Driver", MB_ICONERROR | MB_OK);

            exit(EXIT_FAILURE);
        }
        else if ((buffer.st_mode & S_IFMT) == S_IFDIR) {

            strcpy(folder, basename);

            if (vterm_sessionid > 0) {
                sprintf(basename, "%s_%d", vterm_hostname, vterm_sessionid);
            }
            else {
                sprintf(basename, "%s", vterm_hostname);
            }

            pos = -1;
        }
        else {

            snprintf(folder, pos + 1, "%s", basename);

            stat(folder, &buffer);
        }
    }

    if (pos >= 0) {

        stat(folder, &buffer);

        if ((buffer.st_mode & S_IFMT) != S_IFDIR) {

            MessageBox(NULL, dupprintf("Fatal Error : Folder '%s' does not exist #1 - exiting program.", folder), "Putty Driver", MB_ICONERROR | MB_OK);

            exit(EXIT_FAILURE);
        }

    }
    else {

        getcwd(cwdpath, MAX_FILENAME_SIZE);

        if (strlen(subfolder) <= 0) {
            snprintf(folder, sizeof(folder), "%s", cwdpath);
        }
        else {
            snprintf(folder, sizeof(folder), "%s\\%s", cwdpath, subfolder);
        }

        stat(folder, &buffer);

        if ((buffer.st_mode & S_IFMT) != S_IFDIR) {

            MessageBox(NULL, dupprintf("Fatal Error : Folder '%s' does not exist #2 - exiting program.", folder), "Putty Driver", MB_ICONERROR | MB_OK);

            exit(EXIT_FAILURE);
        }

        if ((checkexists == true) && (stat(filepath, &buffer) == 0)) {

            MessageBox(NULL, dupprintf("Fatal Error : Log file '%s' already exists #2 - exiting program.", filepath), "Putty Driver", MB_ICONERROR | MB_OK);

            exit(EXIT_FAILURE);

        }

        if (timestamp == true) {

            //time_t now = time(NULL);

            //strftime(datetime, 20, "%Y%m%d_%H%M%S", localtime(&now));

            //snprintf(filepath, sizeof(filepath), "%s\\%s_%s.%s", folder, basename, datetime, filesuffix);
            snprintf(filepath, sizeof(filepath), "%s\\%s_%s.%s", folder, basename, vTerm.SessionTimeStamp, filesuffix);           
        }
        else {
            snprintf(filepath, sizeof(filepath), "%s\\%s.%s", folder, basename, filesuffix);
        }
    }

    return filepath;
}

void vTermInitialiseLogs() {

   // char datetime[MAX_STRING_LENGTH];

    if (vterm_nolog != true) {

        if (file_exists(vterm_log_file) == true) {

            MessageBox(NULL, dupprintf("Fatal Error : Log file '%s' already exists #3 - exiting program.", vterm_log_file), "Putty Driver", MB_ICONERROR | MB_OK);

            exit(EXIT_FAILURE);

        }

        vTermLog_Stream = fopen(vterm_log_file, "w");

        if (vTermLog_Stream != NULL) {

            if (vTermLog_Execution == true) {
                fprintf(vTermLog_Stream, "Stack Space|SessionID|Command_Seq|Function_Name|Function Offset|Actual_New|Expected_Previous|Screen_Cursor.Y|Screen_Cursor.X|Screen_Cursor_Prev_Y|Screen_Cursor_Prev_X|Command_Current_Seq|Screen_Command_Seq_From\n");
            }
            else {
                fprintf(vTermLog_Stream, "SessionID|Command_Seq|Function_Name|Function Offset|Actual_New|Expected_Previous|Screen_Cursor.Y|Screen_Cursor.X|Screen_Cursor_Prev_Y|Screen_Cursor_Prev_X|Command_Current_Seq|Screen_Command_Seq_From\n");
            }

            //time_t now = time(NULL);

            //strftime(datetime, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));

            fprintf(vTermLog_Stream, "%d|%s|Starting\n", vTerm.Session_ID, vTerm.SessionTimeStamp);

            fflush(vTermLog_Stream);

        }
        else {

            MessageBox(NULL, dupprintf("Fatal Error : Log file '%s' create failed - exiting program.", vterm_log_file), "Putty Driver", MB_ICONERROR | MB_OK);

            exit(EXIT_FAILURE);

        }
    }
}

void vTermWriteToLog( char* FunctionName, char* Actual_Data, char* Expected_Data) {

    char log_data[MAX_RAWDATA_LEN];

    if (vterm_nolog != true) {

        if (vTermLog_Stream != NULL) {

            if (vTermLog_Execution == true) {
                //sprintf(log_data, "%llu|%d|%d|%s|%s|%s|%d|%d|%d|%d|%d|%d", GetAvailableStackSpace(), vterm_sessionid, vTerm.Command_Seq, FunctionName, Actual_Data, Expected_Data, vTerm.Screen_Cursor.Y, vTerm.Screen_Cursor.X, vTerm.Screen_Cursor_Prev_Y, vTerm.Screen_Cursor_Prev_X, vTerm.Command_Current_Seq, vTerm.Screen_Command_Seq_From);
                sprintf(log_data, "%d|%d|%s|%s|%s|%d|%d|%d|%d|%d|%d", vterm_sessionid, vTerm.Command_Seq, FunctionName, Actual_Data, Expected_Data, vTerm.Screen_Cursor.Y, vTerm.Screen_Cursor.X, vTerm.Screen_Cursor_Prev_Y, vTerm.Screen_Cursor_Prev_X, vTerm.Command_Current_Seq, vTerm.Screen_Command_Seq_From);
            }
            else {
                sprintf(log_data, "%d|%d|%s|%s|%s|%d|%d|%d|%d|%d|%d", vterm_sessionid, vTerm.Command_Seq, FunctionName, Actual_Data, Expected_Data, vTerm.Screen_Cursor.Y, vTerm.Screen_Cursor.X, vTerm.Screen_Cursor_Prev_Y, vTerm.Screen_Cursor_Prev_X, vTerm.Command_Current_Seq, vTerm.Screen_Command_Seq_From);
            }

            fprintf(vTermLog_Stream, "%s\n", string_replacechar(log_data, '\r', ' '));

            fflush(vTermLog_Stream);
        }

    }
}

char* vTermSessionGetValue( int Command_Pos, int Command_Seq) {

    if (Command_Seq <= 0) {
        Command_Seq = vTerm.Command_Seq;
    }

    if (Command_Pos >= vTerm_DBRecord_Session_Cmd_ID_pos) {
        Command_Pos = Command_Pos + vTerm.Screen_Capture_Offset;
    }

    return vTermCommands[Command_Seq][Command_Pos];
}

void ReadKeyCodesFromFile() {

    char cwdpath[MAX_FILENAME_SIZE];

    char input[MAX_BUFFER_SIZE];

    char keycode[MAX_STRING_LENGTH];

    FILE* stream;

    int num = 0;
    int seq = 0;

    if (strlen(vterm_keycodes_file) == 0) {

        getcwd(cwdpath, MAX_FILENAME_SIZE);

        snprintf(vterm_keycodes_file, sizeof(vterm_keycodes_file), "%s\\Scripts\\KeyCodes_Default.txt", cwdpath);
    }

    if (file_exists(vterm_keycodes_file) != true) {

        MessageBox(NULL, dupprintf("Fatal Error : Key Codes File '%s' does not exist - exiting program.", vterm_keycodes_file), "Putty Driver", MB_ICONERROR | MB_OK);

        exit(EXIT_FAILURE);
    }

    stream = fopen(vterm_keycodes_file, "r");

    if (stream == NULL) {

        MessageBox(NULL, dupprintf("Fatal Error : Cannot open Key Codes File '%s' - exiting program.", vterm_keycodes_file), "Putty Driver", MB_ICONERROR | MB_OK);
        
        exit(EXIT_FAILURE);
    }

    while (fgets(input, sizeof(input), stream)) {

        num = string_split(input, '|', vTerm_KeyCode_Elements, MAX_STRING_LENGTH);

        if (num <= 0) {

            MessageBox(NULL, dupprintf("Fatal Error : Data mismatch reading Key Codes File File '%s' line %d - exiting program.", cwdpath, seq), "Putty Driver", MB_ICONERROR | MB_OK);

            fclose(stream);

            exit(EXIT_FAILURE);

        }
        else {

            strcpy(vTermKeyCodes[seq][vTerm_KeyName], ifnull(String_Array[vTerm_KeyName], "0"));

            if (strlen(String_Array[vTerm_KeyANSI]) >= 5) {

                if (strncmp(String_Array[vTerm_KeyANSI], "<esc>", 5) == 0) {

                    memset(keycode, 0, sizeof(keycode));

                    strncpy(keycode, String_Array[vTerm_KeyANSI] + 5, strlen(String_Array[vTerm_KeyANSI]) - 5);

                    sprintf(vTermKeyCodes[seq][vTerm_KeyANSI], "%c%s", 27, keycode);

                }
                else
                    strcpy(vTermKeyCodes[seq][vTerm_KeyANSI], String_Array[vTerm_KeyANSI]);

            }
            else {
                strcpy(vTermKeyCodes[seq][vTerm_KeyANSI], String_Array[vTerm_KeyANSI]);
            }

            seq++;
        }
    }

    fclose(stream);
}

void ReadCommandsFromFile() {

    char cwdpath[MAX_FILENAME_SIZE];

    char input[MAX_BUFFER_SIZE];

    FILE* stream;

    int num = 0;

    if (strlen(trim(vterm_script_file)) == 0) {
        return;
    }

    if (file_exists(vterm_script_file) != true) {

        getcwd(cwdpath, MAX_FILENAME_SIZE);

        snprintf(vterm_script_file, sizeof(cwdpath), "%s\\Scripts\\%s", cwdpath, dupstr(vterm_script_file));
    }

    if (file_exists(vterm_script_file) != true) {

        MessageBox(NULL, dupprintf("Fatal Error : Script Commands File '%s' does not exist - exiting program.", vterm_script_file), "Putty Driver", MB_ICONERROR | MB_OK);

        exit(EXIT_FAILURE);
    }

    stream = fopen(vterm_script_file, "r");

    if (stream == NULL) {

        MessageBox(NULL, dupprintf("Fatal Error : Cannot open Script Commands File '%s' - exiting program.", vterm_script_file), "Putty Driver", MB_ICONERROR | MB_OK);

        exit(EXIT_FAILURE);
    }
    
    vTerm.Command_Seq_Max = 0;
    
    while (fgets(input, sizeof(input), stream)) {

        if (strlen(input) > vTerm_Command_Elements) {

            vTerm.Command_Seq_Max++;

            num = string_split(input, '|', vTerm_Command_Elements, MAX_STRING_LENGTH);

            if (num != vTerm_Command_Elements) {

                MessageBox(NULL, dupprintf("Fatal Error : Data mismatch reading Script Commands File '%s' line %d - exiting program.", vterm_script_file, vTerm.Command_Seq_Max + 1), "Putty Driver", MB_ICONERROR | MB_OK);

                fclose(stream);

                exit(EXIT_FAILURE);

            }
            else {

                strcpy(vTermCommands[vTerm.Command_Seq_Max][vTerm_Command_Seq_pos], ifnull(String_Array[vTerm_Command_Seq_pos], "0"));
                strcpy(vTermCommands[vTerm.Command_Seq_Max][vTerm_Expected_Screen_Identifier_pos], ifnull(String_Array[vTerm_Expected_Screen_Identifier_pos], ""));
                strcpy(vTermCommands[vTerm.Command_Seq_Max][vTerm_Expected_Screen_Identifier_At_pos], ifnull(String_Array[vTerm_Expected_Screen_Identifier_At_pos], ""));
                strcpy(vTermCommands[vTerm.Command_Seq_Max][vTerm_Screen_Capture_OnOff_pos], ifnull(String_Array[vTerm_Screen_Capture_OnOff_pos], ""));
                strcpy(vTermCommands[vTerm.Command_Seq_Max][vTerm_Expected_Command_Prompt_pos], ifnull(String_Array[vTerm_Expected_Command_Prompt_pos], ""));
                strcpy(vTermCommands[vTerm.Command_Seq_Max][vTerm_Expected_Command_Prompt_At_pos], ifnull(String_Array[vTerm_Expected_Command_Prompt_At_pos], ""));
                strcpy(vTermCommands[vTerm.Command_Seq_Max][vTerm_Expected_Input_Cursor_At_pos], ifnull(String_Array[vTerm_Expected_Input_Cursor_At_pos], ""));
                strcpy(vTermCommands[vTerm.Command_Seq_Max][vTerm_Command_Send_pos], ifnull(String_Array[vTerm_Command_Send_pos], ""));
                strcpy(vTermCommands[vTerm.Command_Seq_Max][vTerm_Command_Input_Hidden_pos], ifnull(String_Array[vTerm_Command_Input_Hidden_pos], ""));
                strcpy(vTermCommands[vTerm.Command_Seq_Max][vTerm_Command_Submit_Key_pos], ifnull(String_Array[vTerm_Command_Submit_Key_pos], ""));
                strcpy(vTermCommands[vTerm.Command_Seq_Max][vTerm_Command_Send_Pause_pos], ifnull(String_Array[vTerm_Command_Send_Pause_pos], "0"));
                strcpy(vTermCommands[vTerm.Command_Seq_Max][vTerm_DBRecord_Script_Cmd_ID_pos], ifnull(String_Array[vTerm_DBRecord_Script_Cmd_ID_pos], "0"));

            }
        }
    }

    fclose(stream);
}

void vTermOpenSessionFiles() {

    if (vTermLog_Execution == true) {
        vTermWriteToLog("vTermOpenSessionFiles|Start", NULL, NULL);
    }

    char l_name[MAX_FILENAME_SIZE];

    if (vterm_nocapture != true) {

        if (strlen(vterm_inputs_file) > 0) {

            if (file_exists(vterm_inputs_file) == true) {

                MessageBox(NULL, dupprintf("Fatal Error : Session inputs capture file '%s' already exists - exiting program.", vterm_capture_file), "Putty Driver", MB_ICONERROR | MB_OK);

                exit(EXIT_FAILURE);

            }

            vTermCaptureInputs_Stream = fopen(vterm_inputs_file, "w");

            if (vTermCaptureInputs_Stream == NULL) {

                MessageBox(NULL, dupprintf("Fatal Error : Session input inputs capture file '%s' create failed - exiting program.", vterm_capture_file), "Putty Driver", MB_ICONERROR | MB_OK);

                exit(EXIT_FAILURE);

            }
        }

        if (strlen(vterm_capture_file) > 0) {

            if (file_exists(vterm_capture_file) == true) {

                MessageBox(NULL, dupprintf("Fatal Error : Session screens capture file '%s' already exists - exiting program.", vterm_capture_file), "Putty Driver", MB_ICONERROR | MB_OK);

                exit(EXIT_FAILURE);

            }

            vTermCapture_Stream = fopen(vterm_capture_file, "w");

            if (vTermCapture_Stream == NULL) {

                MessageBox(NULL, dupprintf("Fatal Error : Session screens capture file '%s' create failed - exiting program.", vterm_capture_file), "Putty Driver", MB_ICONERROR | MB_OK);

                exit(EXIT_FAILURE);

            }
        }

        fprintf(vTermCapture_Stream, "<?xml version=""1.0"" encoding=""UTF-8"" standalone=""yes""?>\n");
        fprintf(vTermCapture_Stream, "<events_log xmlns:xsi=""http://www.w3.org/2001/XMLSchema-instance"">\n");

fprintf(vTermCapture_Stream, dupprintf("<user>%s</user>\n", get_username()));
fprintf(vTermCapture_Stream, dupprintf("<server>%s</server>\n", vterm_hostname));
fprintf(vTermCapture_Stream, dupprintf("<script>%s</script>\n", vterm_script_file));
/* fprintf(vTermCapture_Stream, dupprintf("<stack_space>%llu</stack_space>\n", GetAvailableStackSpace())); */
fprintf(vTermCapture_Stream, dupprintf("<process_start>%s</process_start>\n", vTerm.SessionTimeStamp));

fflush(vTermCapture_Stream);
        }

        if (vTermLog_Execution == true) {
            vTermWriteToLog("vTermOpenSessionFiles|Finish", NULL, NULL);
        }
}

void vTermWriteSessionToFile() {

    char log_data[MAX_RAWDATA_LEN];

    if (vTermLog_Execution == true) {
        vTermWriteToLog("vTermWriteSessionToFile|Start", NULL, NULL);
    }

    if (vterm_nocapture != true) {

        if (vTermCapture_Stream == NULL) {

            MessageBox(NULL, dupprintf("Fatal Error : Session commands file '%s' create failed - exiting program.", vterm_capture_file), "Putty Driver", MB_ICONERROR | MB_OK);

            exit(EXIT_FAILURE);

        }

        fprintf(vTermCapture_Stream, "%s\n", vTerm.Commands_Processed);

        if (vTerm.Screen_Capture_Pending == true) {

            fprintf(vTermCapture_Stream, "<Commands_Processed_Screen>%d%c%d%c%d%c</Commands_Processed_Screen>\n", vterm_sessionid, DBDelimiter, vTerm.Screen_Capture_Command_Seq_From, DBDelimiter, vTerm.Screen_Command_Seq_To, DBDelimiter);

            vTerm.Screen_Capture_Command_Seq_From = vTerm.Screen_Command_Seq_To + 1;

            sprintf(log_data, "%s", vTerm.Screen);

            fprintf(vTermCapture_Stream, "<Screen>\n%s\n</Screen>\n", rtrim(string_replacechar(vTerm.Screen, '\r', ' ')));
        }

        fflush(vTermCapture_Stream);

        if (vTerm.Command_Seq > 8) {
            vTerm.Command_Seq = vTerm.Command_Seq;
        }

        if (vTermCaptureInputs_Stream != NULL) {

            fprintf(vTermCaptureInputs_Stream, "%s\n", vTerm.Commands_Input);

            fflush(vTermCaptureInputs_Stream);
        }
    }

    if (vTermLog_Execution == true) {
        vTermWriteToLog("vTermWriteSessionToFile|Finish", NULL, NULL);
    }
}

void vTermCloseSessionLogs() {

    if (vTermLog_Execution == true) {
        vTermWriteToLog("vTermCloseSessionLogs", NULL, NULL);
    }

    vTermSessionTimeStamp();

    if (vTermCapture_Stream != NULL) {

        vTermWriteSessionToFile();

        if (vTermCaptureInputs_Stream != NULL) {

            fflush(vTermCaptureInputs_Stream);

            fclose(vTermCaptureInputs_Stream);

            vTermCaptureInputs_Stream = NULL;

        }

        fprintf(vTermCapture_Stream, dupprintf("<process_finish>%s</process_finish>\n", vTerm.SessionTimeStamp));

        fprintf(vTermCapture_Stream, "</events_log>\n");

        fflush(vTermCapture_Stream);

        fclose(vTermCapture_Stream);

        vTermCapture_Stream = NULL;
    }

    if (vTermLog_Stream != NULL) {

        if (vTerm.Command_Seq_Max > 0) {
            fprintf(vTermLog_Stream, "%d|%s|Processed %d of %d Commands\n", vTerm.Session_ID, vTerm.SessionTimeStamp, vTerm.Command_Seq - 1, vTerm.Command_Seq_Max);
        }
        else {
            fprintf(vTermLog_Stream, "%d|%s|Processed %d Commands\n", vTerm.Session_ID, vTerm.SessionTimeStamp, vTerm.Command_Seq - 1);
        }

        fclose(vTermLog_Stream);

        vTermLog_Stream = NULL;
    }
}

void vTermSessionSetValue( char* Command_Value, int Command_Pos, int Command_Seq) {

    if (Command_Seq <= 0) {
        Command_Seq = vTerm.Command_Seq;
    }

    if (Command_Pos >= vTerm_DBRecord_Session_Cmd_ID_pos) {
        Command_Pos = Command_Pos + vTerm.Screen_Capture_Offset;
    }
    else {
        strcpy(vTermCommands[Command_Seq][Command_Pos], Command_Value);
    }
}

int vTermScreenWrapAdjust( int Screen_Row) {

    int adjust;
    int row;

    adjust = 0;

    row = Screen_Row;

    while (row > 0) {

        if (strlen(vTerm.Screen_Array[row]) > vTerm.Screen_Columns_X) {
            adjust = adjust + (int)(strlen(vTerm.Screen_Array[row]) / (double)vTerm.Screen_Columns_X);
        }

        row = row - 1;
    }

    return adjust;
}

char* vTermScreenTextPosition( char* ScreenText, bool CursorPos) {

    static char vTermScreenTextPositionRet[MAX_STRING_LENGTH];

    int pos;
    int row;

    vTermScreenText_Pos_X = -1;
    vTermScreenText_Pos_Y = -1;

    memset(vTermScreenTextPositionRet,0, MAX_STRING_LENGTH);

    row = MAX_SCREEN_ROWS - 1;

    while (row >= 0) {
        
        if (strlen(trim(vTerm.Screen_Array[row])) > 0) break;

        row--;
    }

    while (row >= 0) {

        if (!CursorPos || row == vTerm.Screen_Cursor.Y) {

            pos = instr(vTerm.Screen_Array[row], ScreenText, 0);

            if (pos >= 0) {

                sprintf(vTermScreenTextPositionRet, "%d,%d", row + vTermScreenWrapAdjust( row - 1), pos);

                vTermScreenText_Pos_X = pos;
                vTermScreenText_Pos_Y = row + vTermScreenWrapAdjust( row - 1);

                break;
            }
        }

        row--;
    }

    return vTermScreenTextPositionRet;
}

void vTermSessionGetScreen( int GetScreen) {

    if (vTermLog_Execution == true) {
        vTermWriteToLog( "vTermSessionGetScreen|Start", dupprintf("%s", GetScreen ? "true" : "false"), NULL);
    }

    if (GetScreen == true) {

        vTerm.Command_Mismatch = false;
            
        vTerm.Screen_Get = true;
            
        vTerm.Screen_Requested_Seq = vTerm.Command_Seq;

        SendMessage((HWND)vTerm.Hwnd, WM_COMMAND, (WPARAM)IDM_CLRSB, (LPARAM)0L);
        SendMessage((HWND)vTerm.Hwnd, WM_COMMAND, (WPARAM)IDM_COPYALL, (LPARAM)0L);

    }
    else if (vTerm.Screen_Get == true) {

        if (vTermLog_Execution == true) {
            vTermWriteToLog( "Waiting for vTermSessionGetScreen|", dupprintf("%d", vTerm.Hwnd), NULL);
        }
    }
    else if (!(vTerm.Screen_Cursor_Prev_X == vTerm.Screen_Cursor.X && vTerm.Screen_Cursor_Prev_Y == vTerm.Screen_Cursor.Y)) {

        vTerm.Screen_Get = true;
        vTerm.Screen_Requested_Seq = vTerm.Command_Seq;

        SendMessage((HWND)vTerm.Hwnd, WM_COMMAND, (WPARAM)IDM_CLRSB, (LPARAM)0L);
        SendMessage((HWND)vTerm.Hwnd, WM_COMMAND, (WPARAM)IDM_COPYALL, (LPARAM)0L);
    }

    if (vTermLog_Execution == true) {
        vTermWriteToLog("vTermSessionGetScreen|Finish", dupprintf("%s", GetScreen ? "true" : "false"), NULL);
    }
}

void vTermSubmitKey( char* CmdKey, bool AnsiSeq) {

    int l_ptr;
    char snum[5];

    char tmpstr[MAX_BUFFER_SIZE];

    if (vTermLog_Execution == true) {
        vTermWriteToLog("vTermSubmitKey|Start", CmdKey, NULL);
    }

    vTerm.Submit_Key_ANSI[0] = '\0';
    vTerm.Submit_Key_Send[0] = '\0';

    vTerm.Submit_Key_Len = 0;

    vTerm.Command_Processed_Submit_Key[0] = '\0';

    vTerm.Command_Processed_Submit_Key_Len = 0;

    sprintf(tmpstr, dupprintf("%s-%d-%s-%d", CmdKey, strlen(CmdKey), trim(CmdKey), strlen(trim(CmdKey))));

    if (strlen(CmdKey) > 0) {

        if (AnsiSeq == true) {

            l_ptr = 0;

            vTerm.Command_Submit_Key = false;

            while (l_ptr < MAX_KEYCODES_SIZE) {

                if ((strlen(CmdKey) == 1 && strcmp(vTermKeyCodes[l_ptr][vTerm_KeyANSI], dupprintf("%d", CmdKey[0])) == 0) || (strcmp(vTermKeyCodes[l_ptr][vTerm_KeyANSI], CmdKey) == 0)) {

                    vTerm.Command_Submit_Key = true;

                    strcpy(vTerm.Command_Processed_Submit_Key, vTermKeyCodes[l_ptr][vTerm_KeyName]);

                    vTerm.Command_Processed_Submit_Key_Len = strlen(CmdKey);

                    l_ptr = 999;
                }

                l_ptr = l_ptr + 1;
            }

            if (vTerm.Command_Submit_Key == true) {

                if (RecordForScripting == true) {

                    if (vTerm.Command_Seq > vTerm.Command_Seq_Max) {

                        vTermSessionSetValue( vTerm.Command_Prompt_Pos, vTerm_Command_Sent_Cursor_At_pos, vTerm.Command_Seq);
                        vTermSessionSetValue( vTerm.Command_Processed, vTerm_Command_Send_pos, vTerm.Command_Seq);
                        vTermSessionSetValue( vTerm.Command_Processed_Submit_Key, vTerm_Command_Submit_Key_pos, vTerm.Command_Seq);
                    }
                }

                vTerm.Command_Processed_Len = 0;

                vTerm.Command_Processed_Manual_Input[0] = '\0';

                vTerm.Command_Processing_Started = false;
                vTerm.Command_Processing_Finished = true;
            }
        }
        else {

            strcpy(vTerm.Submit_Key_Send, CmdKey);

            vTerm.Submit_Key_Len = strlen(CmdKey);

            l_ptr = 0;

            while (l_ptr < MAX_KEYCODES_SIZE) {

                if (strcmp(vTermKeyCodes[l_ptr][vTerm_KeyName], CmdKey) == 0) {

                    if (isnumeric(vTermKeyCodes[l_ptr][vTerm_KeyANSI]) == true) {

                        vTerm.Submit_Key_Send[0] = atoi(vTermKeyCodes[l_ptr][vTerm_KeyANSI]);
                        vTerm.Submit_Key_Send[1] = '\0';

                        strcpy(vTerm.Submit_Key_ANSI, vTerm.Submit_Key_Send);

                        vTerm.Submit_Key_Len = 1;

                    }
                    else {

                        memset(vTerm.Submit_Key_ANSI, 0, sizeof(vTerm.Submit_Key_ANSI));

                        strncpy(vTerm.Submit_Key_ANSI, vTermKeyCodes[l_ptr][vTerm_KeyANSI], strlen(vTermKeyCodes[l_ptr][vTerm_KeyANSI]));

                        vTerm.Submit_Key_Len = strlen(vTerm.Submit_Key_ANSI);

                    }

                    l_ptr = 999;
                }

                l_ptr = l_ptr + 1;
            }
        }
    }

    if (vTermLog_Execution == true) {
        vTermWriteToLog("vTermSubmitKey|Finish", CmdKey, NULL);
    }
}

char* vTermGetCommand( int commandpos, int isnumber) {

    if (isnumber && strlen(vTermCommands[vTerm.Command_Seq][commandpos]) == 0) {
        return "0";
    }
    else {
        return vTermCommands[vTerm.Command_Seq][commandpos];
    }

}

void vTermSetCommand() {

    char* l_exp_xy;

    if (vTermLog_Execution == true) {
        vTermWriteToLog( "vTermSetCommand|Start", NULL, NULL);
    }

    if ((vTerm.Command_Seq > 1) && (vTerm.Command_Mismatch == true)) {

        if (vTermLog_Execution == true) {
            vTermWriteToLog("vTermSetCommand|Return", "Command Mismatch = True", NULL);
        }

        return;
    }

    vTerm.Command_Auto = false;

    strcpy(vTerm.Command_Screen_Identifier, vTermGetCommand( vTerm_Expected_Screen_Identifier_pos, false));
    strcpy(vTerm.Command_Screen_Identifier_Pos, vTermGetCommand( vTerm_Expected_Screen_Identifier_At_pos, false));

    strcpy(vTerm.Command_Prompt_Expected, vTermGetCommand( vTerm_Expected_Command_Prompt_pos, false));
    strcpy(vTerm.Command_Prompt_Expected_Pos, vTermGetCommand( vTerm_Expected_Command_Prompt_At_pos, false));

    strcpy(vTerm.Command_Send_Expected_Cursor, vTermGetCommand( vTerm_Expected_Input_Cursor_At_pos, false));

    strcpy(vTerm.Command_Send, vTermGetCommand( vTerm_Command_Send_pos, false));

    vTerm.Command_Send_Pause = atoi(vTermGetCommand( vTerm_Command_Send_Pause_pos, true));

    vTerm.Command_Script_DB_ID = atol(vTermGetCommand( vTerm_DBRecord_Script_Cmd_ID_pos, true));

    strcpy(vTerm.Command_Input_Hidden, vTermGetCommand( vTerm_Command_Input_Hidden_pos, false));

    strcpy(vTerm.Screen_Capture, vTermGetCommand( vTerm_Screen_Capture_OnOff_pos, false));

    strcpy(vTerm.Submit_Key, vTermGetCommand( vTerm_Command_Submit_Key_pos, false));

    vTerm.Command_Screen_Identifier_Len = strlen(vTerm.Command_Screen_Identifier);

    vTerm.Command_Screen_Identifier_Pos_Y = -1;
    vTerm.Command_Screen_Identifier_Pos_X = -1;

    l_exp_xy = strtok(dupprintf("%s", vTerm.Command_Screen_Identifier_Pos), ",");

    if (l_exp_xy != NULL) {

        if (strcmp(l_exp_xy, "*") != 0) vTerm.Command_Screen_Identifier_Pos_Y = atoi(l_exp_xy);

        l_exp_xy = strtok(NULL, ",");

        if (l_exp_xy != NULL) {
            if (strcmp(l_exp_xy, "*") != 0) vTerm.Command_Screen_Identifier_Pos_X = atoi(l_exp_xy);
        }
    }

    vTerm.Command_Prompt_OK[0] = '\0';

    vTerm.Command_Prompt_Expected_Len = strlen(vTerm.Command_Prompt_Expected);

    vTerm.Command_Prompt_Expected_Pos_Y = -1;
    vTerm.Command_Prompt_Expected_Pos_X = -1;

    l_exp_xy = strtok(dupprintf("%s", vTerm.Command_Prompt_Expected_Pos), ",");

    if (l_exp_xy != NULL) {

        if (strcmp(l_exp_xy, "*") != 0) vTerm.Command_Prompt_Expected_Pos_Y = atoi(l_exp_xy);

        l_exp_xy = strtok(NULL, ",");

        if (l_exp_xy != NULL) {
            if (strcmp(l_exp_xy, "*") != 0) vTerm.Command_Prompt_Expected_Pos_X = atoi(l_exp_xy);
        }
    }

    l_exp_xy = strtok(dupprintf("%s",vTerm.Command_Send_Expected_Cursor), ",");

    vTerm.Command_Send_Expected_Cursor_Y = -1;
    vTerm.Command_Send_Expected_Cursor_X = -1;

    if (l_exp_xy != NULL) {

        if (strcmp(l_exp_xy, "*") != 0) vTerm.Command_Send_Expected_Cursor_Y = atoi(l_exp_xy);

        l_exp_xy = strtok(NULL, ",");

        if (l_exp_xy != NULL) {
            if (strcmp(l_exp_xy, "*") != 0) vTerm.Command_Send_Expected_Cursor_X = atoi(l_exp_xy);
        }
    }

    vTerm.Screen_Identifier[0] = '\0';
    vTerm.Screen_Identifier_Pos[0] = '\0';

    vTerm.Command_Prompt[0] = '\0';
    vTerm.Command_Prompt_Len = 0;
    vTerm.Command_Prompt_Pos[0] = '\0';
    vTerm.Command_Prompt_OK[0] = '\0';

    vTerm.Command_Sent[0] = '\0';
    vTerm.Command_Sent_Len = 0;
    vTerm.Command_Sent_Cursor_Pos[0] = '\0';

    vTerm.Command_Mismatch = false;
    vTerm.Command_Mismatch_Type[0] = '\0';
    vTerm.Command_Mismatch_Pos_Expected[0] = '\0';
    vTerm.Command_Mismatch_Pos_Actual[0] = '\0';

    vTerm.Command_Processed[0] = '\0';
    vTerm.Command_Processed_Len = 0;
    vTerm.Command_Processed_Submit_Key[0] = '\0';

    vTerm.Command_Processing = false;
    vTerm.Command_Processing_Started = false;
    vTerm.Command_Processing_Finished = false;

    if (vTerm.Command_Send_Pause <= 0) {
        vTerm.Command_Wait_Until = time(NULL) - 999;
    }
    else {
        vTerm.Command_Wait_Until = time(NULL) + vTerm.Command_Send_Pause;
    }

    vTerm.Command_Send_At = time(NULL);

    vTerm.Command_Submit_Key = false;

    vTerm.Command_Send_Len = strlen(vTerm.Command_Send);
    vTerm.Command_Send_Buffer_Len = vTerm.Command_Send_Len;

    vTerm.Command_Send_Pos = 0;

    vTerm.Submit_Key_ANSI[0] = '\0';
    vTerm.Submit_Key_Send[0] = '\0';
    vTerm.Submit_Key_Len = 0;

    vTermSubmitKey( vTerm.Submit_Key, false);

    if (vTerm.Command_Prompt_Expected_Len + vTerm.Command_Send_Buffer_Len + vTerm.Submit_Key_Len > 0) {
        vTerm.Command_Auto = true;
    }
    else {
        vTerm.Command_Auto = false;
    }

    if (vTermLog_Execution == true) {
        vTermWriteToLog( "vTermSetCommand|Finish", NULL, NULL);
    }
}

void vTermCommandMismatch( char* MismatchType, char* Actual_Pos, char* Expected_Pos) {

    if (vTermLog_Execution == true) {
        vTermWriteToLog(dupprintf("vTermCommandMismatch - %s|Start", MismatchType), Actual_Pos, Expected_Pos);
    }

    strcpy(vTerm.Command_Prompt_OK, "No");

    vTermSessionSetValue( vTerm.Command_Prompt_OK, 0, 0);

    if (vTerm.Screen_Requested_Seq < vTerm.Command_Seq) {
        vTermSessionGetScreen( false);
    }
    else {

        strcpy(vTerm.Command_Mismatch_Type, MismatchType);
        strcpy(vTerm.Command_Mismatch_Pos_Expected, Expected_Pos);
        strcpy(vTerm.Command_Mismatch_Pos_Actual, Actual_Pos);

        if (!strstr(MismatchType, "Position Mismatch") == NULL) {

            vTerm.Command_Mismatch = true;

            vTermWriteToLog(dupprintf("vTermCommandMismatch|%s", MismatchType), Actual_Pos, Expected_Pos);

        }
    }

    if (vTermLog_Execution == true) {
        vTermWriteToLog(dupprintf("vTermCommandMismatch - %s|Finish", MismatchType), Actual_Pos, Expected_Pos);
    }
}

void SendChars(long Hwnd, char* sChars, bool SysKey) {

    char send;
    long ret;

    if (vTermLog_Execution == true) {
        vTermWriteToLog("SendChars|", sChars, NULL);
    }

    for (int i = 0; sChars[i] != '\0'; i++) {

        send = sChars[i];

        if (SysKey == true) {
            SendMessage((HWND)Hwnd, WM_KEYDOWN, (WPARAM)sChars[i], (LPARAM)0L);
        }
        else {
            SendMessage((HWND)Hwnd, WM_CHAR, (WPARAM)sChars[i], (LPARAM)0L);
        }
    }
}

void vTermCommandSend( bool FullCommand) {

    bool l_submit;

    if (vTermLog_Execution == true) {
        vTermWriteToLog("vTermCommandSend|Start", NULL, NULL);
    }

    l_submit = false;

    if (vTerm.Command_Send_Buffer_Len > 0) {

        if (FullCommand || strcmp(vTerm.Command_Input_Hidden, "Yes") == 0) {

            strcpy(vTerm.Command_Sent, vTerm.Command_Send);

            vTerm.Command_Sent_Len = vTerm.Command_Send_Buffer_Len;

            vTerm.Command_Send_Pos = vTerm.Command_Send_Buffer_Len;
            vTerm.Command_Send_Buffer_Len = 0;

            SendChars(vTerm.Hwnd, vTerm.Command_Send, false);

            vTerm.Command_Send_At = time(NULL);
        }
        else {

            vTerm.Command_Send_Pos += 1;

            strncat(vTerm.Command_Sent, &vTerm.Command_Send[vTerm.Command_Send_Pos - 1], 1);
            vTerm.Command_Sent_Len += 1;

            vTerm.Command_Send_Buffer_Len -= 1;

            char chr[2];
  
            chr[0]= vTerm.Command_Send[vTerm.Command_Send_Pos - 1];
            chr[1] = '\0';

            SendChars(vTerm.Hwnd, chr, false);

            vTerm.Command_Send_At = time(NULL);
        }
    }
    else if (vTerm.Submit_Key_Len > 0) {

        vTerm.Command_Current_Seq = vTerm.Command_Seq;

        strcpy(vTerm.Command_Prompt, vTerm.Command_Prompt_Expected);

        vTerm.Command_Prompt_Len = vTerm.Command_Prompt_Expected_Len;

        SendChars(vTerm.Hwnd, vTerm.Submit_Key_ANSI, false);

        vTerm.Submit_Key_Len = 0;

        vTerm.Command_Send_At = time(NULL);

        vTerm.Command_Processing = false;
    }

    if (vTermLog_Execution == true) {
        vTermWriteToLog("vTermCommandSend|Finish", NULL, NULL);
    }
}

void vTermSendCommand() {

    int l_screen_pos;
    int l_sid;
    int l_act_X;
    int l_act_Y;
    int l_exp_X;
    int l_exp_Y;

    char* l_control;

    if (vTermLog_Execution == true) {
        vTermWriteToLog( "vTermSendCommand|", vTerm.Command_Sent, vTerm.Command_Send);
    }

    if (vTerm.Command_Seq > vTerm.Command_Seq_Max) {

        if (vTermLog_Execution == true) {
            vTermWriteToLog("vTermSendCommand|Return", dupprintf("No data to send - script finished at command %d", vTerm.Command_Seq_Max), NULL);
        }

        return;
    }

    if (vTerm.Command_Send_Buffer_Len + vTerm.Submit_Key_Len <= 0) {

        if (vTermLog_Execution == true) {
            vTermWriteToLog("vTermSendCommand|Return", "No input command set", NULL);
        }

        return;
    }

    if (vTerm.Command_Mismatch == true) {

        if (vTermLog_Execution == true) {
            vTermWriteToLog( "vTermSendCommand|Return", "Command Mismatch = True", NULL);
        }

        return;
    }

    if (vTerm.Command_Wait_Until > time(NULL)) {

        if (vTermLog_Execution == true) {
            vTermWriteToLog( "vTermSendCommand|Waiting", NULL, vTerm.Command_Send);
        }

        return;
    }

    bool l_waiting = vTerm.Command_Wait;

    vTerm.Command_Wait = 0;

    bool l_proc = true;

    if (vTerm.Command_Current_Seq < vTerm.Command_Seq && vTerm.Command_Send_Buffer_Len + vTerm.Submit_Key_Len > 0) {

        if (vTerm.Command_Send_Expected_Cursor_Y < 0 || vTerm.Command_Send_Expected_Cursor_Y == vTerm.Screen_Cursor.Y) {

            if (vTerm.Command_Send_Expected_Cursor_X < 0 || 
                vTerm.Command_Send_Expected_Cursor_X == vTerm.Screen_Cursor.X - vTerm.Command_Send_Pos || 
                (vTerm.Command_Send_Expected_Cursor_X == vTerm.Screen_Cursor.X && strstr(vTerm.Command_Input_Hidden, "Yes") != NULL)) {

                if (SessionsKeyPressSync == true || vTerm.Command_Send_Pos <= 0 || vTerm.Command_Send_Pos == vTerm.Command_Send_Buffer_Len) {
                    vTermSessionGetScreen( false);
                }

                l_proc = true;

                if (vTerm.Command_Screen_Identifier_Len > 0) {

                    strcpy(vTerm.Screen_Identifier_Pos, vTermScreenTextPosition(vTerm.Command_Screen_Identifier, false));

                    if (strlen(trim(vTerm.Screen_Identifier_Pos)) > 0) {
                        vTermSessionSetValue( vTerm.Screen_Identifier_Pos, vTerm_Screen_Identifier_At_pos, vTerm.Command_Seq);
                    }

                    if (strstr(vTerm.Screen, vTerm.Command_Screen_Identifier) != NULL) {

                        if (strlen(trim(vTerm.Command_Screen_Identifier_Pos)) > 0) {

                            if (vTerm.Command_Screen_Identifier_Pos_Y < 0 || vTerm.Command_Screen_Identifier_Pos_Y == vTermScreenText_Pos_Y) {

                                if (vTerm.Command_Screen_Identifier_Pos_X < 0 || vTerm.Command_Screen_Identifier_Pos_X == vTermScreenText_Pos_X) {

                                    vTermSessionSetValue(vTerm.Screen_Identifier_Pos, vTerm_Screen_Identifier_At_pos, vTerm.Command_Seq);

                                    if (vTermLog_Execution == true) {
                                        vTermWriteToLog("Screen Identifier Position Matches OK (Y,X)", vTerm.Screen_Identifier_Pos, vTerm.Command_Screen_Identifier_Pos);
                                    }
                                }
                                else {

                                    l_proc = false;

                                    vTerm.Screen_Identifier_Pos[0] = '\0';

                                    if (vTermLog_Execution == true) {
                                        vTermCommandMismatch("Screen Identifier Position Mismatch (X)", dupprintf("%d", vTermScreenText_Pos_X), dupprintf("%d", vTerm.Command_Screen_Identifier_Pos_X));
                                    }
                                }
                            }
                            else {

                                l_proc = false;

                                vTerm.Screen_Identifier_Pos[0] = '\0';

                                if (vTermLog_Execution == true) {
                                    vTermCommandMismatch("Screen Identifier Position Mismatch (Y)", dupprintf("%d", vTermScreenText_Pos_Y), dupprintf("%d", vTerm.Command_Screen_Identifier_Pos_Y));
                                }
                            }
                        }
                    }
                    else {

                        l_proc = false;

                        vTermCommandMismatch("Screen Identifier Mismatch", "", vTerm.Command_Screen_Identifier);
                    }
                }

                if ((l_proc == true) && (vTerm.Command_Prompt_Expected_Len > 0)) {

                    if ((vTerm.Screen_Cursor.X < 0) || (strlen(trim(vTerm.Screen))) <= 0) {

                        if (vTermLog_Execution == true) {

                            if (vTerm.Screen_Cursor.X < 0)
                                vTermWriteToLog("vTermSendCommand|Screen_Cursor.X < 0", NULL, NULL);

                            if (strlen(trim(vTerm.Screen)) <= 0)
                                vTermWriteToLog("vTermSendCommand|Screen < 0", NULL, NULL);
                        }

                        return;
                    }

                    l_screen_pos = instrrev(vTerm.Screen, vTerm.Command_Prompt_Expected);

                    if (l_screen_pos > vTerm.Screen_Ptr) {

                        strcpy(vTerm.Command_Prompt_Pos, vTermScreenTextPosition(vTerm.Command_Prompt_Expected, false));

                        if (strlen(trim(vTerm.Command_Prompt_Pos)) > 0) {
                            vTermSessionSetValue( vTerm.Command_Prompt_Pos, vTerm_Command_Prompt_At_pos, vTerm.Command_Seq);
                        }

                        if (strlen(trim(vTerm.Command_Prompt_Expected_Pos)) > 0) {

                            if (vTerm.Command_Prompt_Expected_Pos_Y < 0 || vTerm.Command_Prompt_Expected_Pos_Y == vTermScreenText_Pos_Y) {

                                if (vTerm.Command_Prompt_Expected_Pos_X < 0 || vTerm.Command_Prompt_Expected_Pos_X == vTermScreenText_Pos_X) {

                                    vTermSessionSetValue(vTerm.Command_Prompt_Pos, vTerm_Command_Prompt_At_pos, vTerm.Command_Seq);

                                    if (vTermLog_Execution == true) {
                                        vTermWriteToLog("Command Prompt Position Matches OK (Y,X)", vTerm.Command_Prompt_Pos, vTerm.Command_Prompt_Expected_Pos);
                                    }

                                }

                                else {

                                    l_proc = false;

                                    vTerm.Command_Prompt_Pos[0] = '\0';

                                    if (vTermLog_Execution == true) {
                                        vTermCommandMismatch("Command Prompt Position Mismatch (X)", dupprintf("%d", vTermScreenText_Pos_X), dupprintf("%d", vTerm.Command_Prompt_Expected_Pos_X));
                                    }
                                }
                            }
                            else {

                                l_proc = false;

                                vTerm.Command_Prompt_Pos[0] = '\0';

                                if (vTermLog_Execution == true) {
                                    vTermCommandMismatch("Command Prompt Position Mismatch (Y)", dupprintf("%d", vTermScreenText_Pos_Y), dupprintf("%d", vTerm.Command_Prompt_Expected_Pos_Y));
                                }
                            }
                        }

                        if (l_proc == false) {
                            vTerm.Screen_Ptr = l_screen_pos;
                        }
                    }
                    else {

                        l_proc = false;

                        strcpy(vTerm.Command_Prompt_OK, "No");

                        vTermSessionSetValue( vTerm.Command_Prompt_OK, vTerm_Command_Prompt_OK_pos, vTerm.Command_Seq);

                        if (vTerm.Screen_Requested_Seq < vTerm.Command_Seq) {
                            vTermSessionGetScreen( false);
                        }
                        else {

                            vTerm.Command_Mismatch = true;

                            if (vTerm.Command_Seq > 1) {
                                l_proc = false;

                                l_sid = instr(vTerm.Screen, "password", 0);

                                if (l_sid > 0) {
                                    l_proc = false;
                                }
                            }

                            if (vTermLog_Execution == true) {
                                vTermCommandMismatch("Command Prompt Mismatch", vTerm.Screen, vTerm.Command_Prompt_Expected);
                            }
                        }
                    }
                }

                if (l_proc == true) {

                    strcpy(vTerm.Command_Prompt_OK, "Yes");

                    vTermSessionSetValue( vTerm.Command_Prompt_OK, vTerm_Command_Prompt_OK_pos, vTerm.Command_Seq);

                    vTerm.Command_Prompt_Expected_Len = -99;

                    if (vTerm.Hwnd > 0 && !(vTerm_Stop == true)) {

                        if (!(vTerm.Command_Wait == true)) {
                            vTermCommandSend( !SessionsKeyPressSync);
                        }
                    }
                }
            }
            else {

                if (vTermLog_Execution == true) {
                    vTermWriteToLog( "vTermSendCommand|Cursor X Position Mismatch", dupprintf("%d", (vTerm.Screen_Cursor.X - vTerm.Command_Send_Pos)), dupprintf("%d", vTerm.Command_Send_Expected_Cursor_X));
                }
            }
        }
        else {

            if (vTermLog_Execution == true) {
                vTermWriteToLog( "vTermSendCommand|Cursor Y Position Mismatch", dupprintf("%d", vTerm.Screen_Cursor.Y), dupprintf("%d", vTerm.Command_Send_Expected_Cursor_Y));
            }
        }
    }

    if (vTermLog_Execution == true) {
        vTermWriteToLog( "vTermSendCommand|Finish", vTerm.Command_Sent, vTerm.Command_Send);
    }
}

void vTermSessionInitialise(int SessionID) {

    vTerm.Command_Seq = 1;
    vTerm.Command_Current_Seq = -1;
    /*vTerm.Command_Seq_Max = -1;*/
    vTerm.Command_Prompt[0] = '\0';
    vTerm.Command_Prompt_Pos[0] = '\0';
    vTerm.Command_Prompt_Len = 0;
    vTerm.Command_Prompt_OK[0] = '\0';
    vTerm.Command_Processed_Manual_Input[0] = '\0';
    vTerm.Command_Mismatch = false;
    vTerm.Command_Mismatch_Type[0] = '\0';
    vTerm.Command_Mismatch_Pos_Expected[0] = '\0';
    vTerm.Command_Mismatch_Pos_Actual[0] = '\0';
    vTerm.Command_Processing = false;
    vTerm.Command_Processing_Started = false;
    vTerm.Command_Processing_Finished = false;
    vTerm.Command_Processed_ASCII[0] = '\0';
    vTerm.Command_Processed[0] = '\0';
    vTerm.Command_Processed_Len = 0;
    vTerm.Command_Current_Seq = -1;
    vTerm.Command_Screen_Identifier[0] = '\0';
    vTerm.Command_Screen_Identifier_Len = 0;
    vTerm.Command_Submit_Key = false;
    vTerm.Command_Wait = false;
    vTerm.Commands_Input[0] = '\0';
    vTerm.Commands_Processed[0] = '\0';
    vTerm.Controller_Updated_Seq = -1;
    vTerm.Screen[0] = '\0';
    vTerm.Screen_Capture_Offset = 0;
    vTerm.Screen_Capture_RGB = 0;
    vTerm.Screen_Get = false;
    vTerm.Screen_Ptr = -1;
    vTerm.Screen_Raw[0] = '\0';
    vTerm.Screen_Raw_ASCII[0] = '\0';
    vTerm.Pid = 0L;
    vTerm.Hwnd = 0L;
    vTerm.Screen_Command_Seq_From = 1;
    vTerm.Screen_Capture_Command_Seq_From = 1;
    vTerm.Screen_Cursor_Prev_X = -1;
    vTerm.Screen_Cursor_Prev_Y = -1;
    
    memset(vTerm.Screen_Array, 0, sizeof(vTerm.Screen_Array));

    vTerm.Screen_Array_Rows = 0;
    vTerm.Screen_New_Rows = 0;

    vTerm_Stop = false;
}

void vTermWaitingForInput( int Cursor_X, int Cursor_Y, int Columns_X, int Rows_Y, bool Command_Processing) {

    if (vTermLog_Execution == true) {
        vTermWriteToLog( "vTermWaitingForInput|Start", dupprintf("%d", Cursor_X), dupprintf("%d", Cursor_Y));
    }

    if (vTerm.Screen_Cursor.X == Cursor_X && vTerm.Screen_Cursor.Y == Cursor_Y && vTerm.Command_Mismatch == true) {

        if (vTermLog_Execution == true) {
            vTermWriteToLog("vTermWaitingForInput|Command Mismatch - Return", dupprintf("%d", Cursor_X), dupprintf("%d", Cursor_Y));
        }

        return;
    }

    vTerm.Screen_Cursor.X = Cursor_X;
    vTerm.Screen_Cursor.Y = Cursor_Y;
    vTerm.Screen_Columns_X = Columns_X;

    vTerm.Screen_Rows_Y = Rows_Y;

    vTerm.Command_Processing = Command_Processing;

    /* Manually typed command. */
    if (vTerm.Command_Seq > vTerm.Command_Seq_Max) {

        if (!(Command_Processing == true)) {

            if (!(vTerm.Screen_Cursor_Prev_X == vTerm.Screen_Cursor.X && vTerm.Screen_Cursor_Prev_Y == vTerm.Screen_Cursor.Y)) {

                if (vTerm.Command_Processed_Len == 1 && !((vTerm.Command_Processed_Manual_Input) == (vTerm.Command_Processed))) {

                    if (!(vTerm.Screen_Cursor_Prev_X + 1 == vTerm.Screen_Cursor.X && vTerm.Screen_Cursor_Prev_Y == vTerm.Screen_Cursor.Y)) {
                        vTerm.Command_Processing_Finished = true;
                    }
                }

                vTermSessionGetScreen(true);

                strcpy(vTerm.Command_Processed_Manual_Input, vTerm.Command_Processed);

                vTerm.Screen_Cursor_Prev_X = vTerm.Screen_Cursor.X;
                vTerm.Screen_Cursor_Prev_Y = vTerm.Screen_Cursor.Y;

                strcpy(vTerm.Command_Current_Cursor_Pos, vTerm.Screen_Cursor.Y + "," + vTerm.Screen_Cursor.X);

                vTermSessionSetValue( vTerm.Command_Current_Cursor_Pos, vTerm_Current_Cursor_pos, vTerm.Command_Seq);
            }
        }
    }
    /* Command is being processed. */
    else if (vTerm.Command_Processing == true) {

        vTermSessionGetScreen( true);

        if (!(vTerm.Screen_Cursor_Prev_X == vTerm.Screen_Cursor.X && vTerm.Screen_Cursor_Prev_Y == vTerm.Screen_Cursor.Y)) {

            strcpy(vTerm.Command_Current_Cursor_Pos, vTerm.Screen_Cursor.Y + "," + vTerm.Screen_Cursor.X);

            vTermSendCommand();
        }
    }
    /* Waiting for input. */
    else if (vTerm.Command_Send_Buffer_Len + vTerm.Submit_Key_Len > 0) {
        vTermSendCommand();
    }
    else if (vTermLog_Execution == true) {
        vTermWriteToLog( "vTermWaitingForInput|No input command set", dupprintf("%d", Cursor_X), dupprintf("%d", Cursor_Y));
    }

    if (vTermLog_Execution == true) {
        vTermWriteToLog( "vTermWaitingForInput|Finish", dupprintf("%d", Cursor_X), dupprintf("%d", Cursor_Y));
    }
}

void vTermSetCommandProcessed()
{
    char command_input[MAX_BUFFER_SIZE];

    if (strcmp(vTerm.Command_Input_Hidden, "Yes") == 0) {
        strcpy(command_input, "##private##");
    }
    else if (vTerm.Command_Seq > vTerm.Command_Seq_Max) {
        strcpy(command_input, vTerm.Command_Processed);
    }
    else {
        strcpy(command_input, vTerm.Command_Send);
    }

    if (strlen(trim(vTerm.Commands_Input)) > 0) {
        append_char(vTerm.Commands_Input, '\n', MAX_BUFFER_SIZE);
    }

    append_string(vTerm.Commands_Input, dupprintf("%d", vTerm.Command_Seq), MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Input, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Input, vTerm.Command_Screen_Identifier, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Input, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Input, vTerm.Command_Screen_Identifier_Pos, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Input, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Input, vTerm.Screen_Capture, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Input, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Input, vTerm.Command_Prompt_Expected, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Input, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Input, vTerm.Command_Prompt_Expected_Pos, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Input, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Input, vTerm.Command_Send_Expected_Cursor, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Input, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Input, command_input, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Input, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Input, vTerm.Command_Input_Hidden, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Input, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Input, vTerm.Submit_Key, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Input, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Input, dupprintf("%d", vTerm.Command_Send_Pause), MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Input, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Input, dupprintf("%d", vTerm.Command_Script_DB_ID), MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Input, DBDelimiter, MAX_BUFFER_SIZE);

    if (strlen(trim(vTerm.Commands_Processed)) > 0) {
        append_char(vTerm.Commands_Processed, '\n', MAX_BUFFER_SIZE);
    }

    if (vTerm.Command_Seq <= vTerm.Command_Seq_Max) {

        append_string(vTerm.Commands_Processed, dupprintf("<Command_Input_Script>%d", vterm_sessionid), MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, dupprintf("%d", vTerm.Command_Seq), MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Command_Screen_Identifier, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Command_Screen_Identifier_Pos, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Screen_Capture, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Command_Prompt_Expected, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Command_Prompt_Expected_Pos, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Command_Send_Expected_Cursor, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, command_input, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Command_Input_Hidden, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Submit_Key, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, dupprintf("%d", vTerm.Command_Send_Pause), MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, dupprintf("%d", vTerm.Command_Script_DB_ID), MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, "</Command_Input_Script>", MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, '\n', MAX_BUFFER_SIZE);

    }
    else {

        append_string(vTerm.Commands_Processed, dupprintf("<Command_Input_User>%d", vterm_sessionid), MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, dupprintf("%d", vTerm.Command_Seq), MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Command_Screen_Identifier, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Command_Screen_Identifier_Pos, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Screen_Capture, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Command_Prompt_Expected, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Command_Prompt_Expected_Pos, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Command_Sent_Cursor_Pos, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, command_input, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Command_Input_Hidden, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, vTerm.Command_Processed_Submit_Key, MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, dupprintf("%d", vTerm.Command_Send_Pause), MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, dupprintf("%d", vTerm.Command_Script_DB_ID), MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
        append_string(vTerm.Commands_Processed, "</Command_Input_User>", MAX_BUFFER_SIZE);
        append_char(vTerm.Commands_Processed, '\n', MAX_BUFFER_SIZE);

        }

    if (strcmp(vTerm.Command_Input_Hidden, "Yes") != 0) {
        strcpy(command_input, vTerm.Command_Processed);
    }

    append_string(vTerm.Commands_Processed, dupprintf("<Command_Processed>%d", vterm_sessionid), MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Processed, dupprintf("%d", vTerm.Command_Seq), MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Processed, vTerm.Command_Screen_Identifier, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Processed, vTerm.Command_Screen_Identifier_Pos, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Processed, vTerm.Screen_Capture, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Processed, vTerm.Command_Prompt, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Processed, vTerm.Command_Prompt_Pos, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Processed, vTerm.Command_Sent_Cursor_Pos, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Processed, command_input, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Processed, vTerm.Command_Input_Hidden, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Processed, vTerm.Command_Processed_Submit_Key, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Processed, vTerm.Command_Prompt_OK, MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Processed, dupprintf("%d", vTerm.Command_Send_Pause), MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Processed, dupprintf("%d", vTerm.Command_Script_DB_ID), MAX_BUFFER_SIZE);
    append_char(vTerm.Commands_Processed, DBDelimiter, MAX_BUFFER_SIZE);
    append_string(vTerm.Commands_Processed, "</Command_Processed>", MAX_BUFFER_SIZE);

    vTerm.Command_Processed_Logging = false;

    if (instr(vTerm.Screen_Capture, "No", 0) >= 0) {
        vTerm.Screen_Capture_Pending = false;
    }
    else {
        vTerm.Screen_Capture_Pending = true;
    }


    if (strcmp(vTerm.Screen_Capture, "No") == 0) {
        vTerm.Screen_Capture_Pending = false;
    }
    else {
        vTerm.Screen_Capture_Pending = true;
    }
}

void vTermProcessData(char* PuttyData, int DataLength, int CommandType) {

    char l_cmd[MAX_BUFFER_SIZE];

    int l_cmd_len;

    bool l_command;
    bool l_submit;
    bool l_submit_key = false;
    bool l_alpha;
    bool l_proc;

    int l_pos;
    int l_ptr;
    int l_ptr2;
    int l_wrap;

    if (vTerm.Pid <= 0) {

        MessageBox(NULL, "Fatal Error : Cannot connect to 'putty'!!", "Putty Driver", MB_ICONERROR | MB_OK);
        
        exit(EXIT_FAILURE);
    }

    if (vTermLog_Execution == true) {
        vTermWriteToLog("vTermProcessData|Start", dupprintf("%d %s", DataLength, PuttyData), NULL);
    }

    if (vTerm.Command_Mismatch == true) {
        return;
    }

    if (strlen(PuttyData) <= 0) {
        return;
    }

    vTerm.Row_Updated_At = time(NULL);

    if (vTerm.Command_Processed_Len <= 0) {
        l_command = true;
    }

    if (DataLength > 0) {

        for (l_pos = 0; l_pos < DataLength; l_pos++) {

            if (PuttyData[l_pos] > 0) {

                if (CommandType == vTerm_Command) {

                    if (strlen(trim(vTerm.Command_Sent_Cursor_Pos)) > 0) {
                        /* Ignore.*/
                    }
                    else {

                        sprintf(vTerm.Command_Sent_Cursor_Pos, "%d,%d", vTerm.Screen_Cursor.Y, vTerm.Screen_Cursor.X);

                        vTermSessionSetValue(vTerm.Command_Sent_Cursor_Pos, vTerm_Command_Sent_Cursor_At_pos, vTerm.Command_Seq);

                        if (vTerm.Command_Seq > vTerm.Command_Seq_Max) {

                            l_wrap = vTermScreenWrapAdjust(vTerm.Screen_Cursor.Y);

                            if (strlen(vTerm.Screen_Array[vTerm.Screen_Cursor.Y - l_wrap]) >= vTerm.Screen_Cursor.X - 1) {

                                l_alpha = false;

                                l_proc = true;

                                l_ptr = vTerm.Screen_Cursor.X;

                                while (l_ptr > 0 && l_proc == true) {

                                    if (vTerm.Screen_Array[vTerm.Screen_Cursor.Y - l_wrap][l_ptr] == ' ') {

                                        if (vTerm.Screen_Array[vTerm.Screen_Cursor.Y - l_wrap][l_ptr] == ' ' &&
                                            vTerm.Screen_Array[vTerm.Screen_Cursor.Y - l_wrap][l_ptr - 1] == ' ') {

                                            l_cmd[0] = '\0';

                                            l_ptr2 = l_ptr;
                                            
                                            l_ptr = l_ptr + 1;

                                            while (l_ptr <= vTerm.Screen_Cursor.X) {
                                                
                                                append_char(l_cmd, vTerm.Screen_Array[vTerm.Screen_Cursor.Y - l_wrap][l_ptr], MAX_BUFFER_SIZE);
                                                
                                                l_ptr = l_ptr + 1;

                                            }

                                            if (strlen(trim(l_cmd)) > 0) {

                                                if (l_alpha == true) {
                                                    vTermSessionSetValue(trim(l_cmd), vTerm_Expected_Command_Prompt_pos, vTerm.Command_Seq);
                                                    vTermSessionSetValue(vTerm.Screen_Cursor.Y + "," + l_ptr2, vTerm_Expected_Command_Prompt_At_pos, vTerm.Command_Seq);
                                                    vTermSessionSetValue(vTerm.Screen_Cursor.Y + "," + l_ptr2, vTerm_Command_Prompt_At_pos, vTerm.Command_Seq);
                                                }

                                            vTermSessionSetValue(vTerm.Command_Sent_Cursor_Pos, vTerm_Expected_Input_Cursor_At_pos, vTerm.Command_Seq);

                                            }

                                            l_proc = false;
                                        }
                                    }
                                    else if (isalnum(vTerm.Screen_Array[vTerm.Screen_Cursor.Y - l_wrap][l_ptr]) == true) {
                                        l_alpha = true;
                                    }

                                    l_ptr = l_ptr - 1;
                                }

                                if (l_proc == true) {

                                    if (l_alpha == true) {

                                        vTermSessionSetValue(trim(vTerm.Screen_Array[vTerm.Screen_Cursor.Y - l_wrap], 1, vTerm.Screen_Cursor.X - 1), vTerm_Expected_Command_Prompt_pos, vTerm.Command_Seq);
                                        vTermSessionSetValue(vTerm.Screen_Cursor.Y + ",0", vTerm_Expected_Command_Prompt_At_pos, vTerm.Command_Seq);
                                        vTermSessionSetValue(vTerm.Screen_Cursor.Y + ",0", vTerm_Command_Prompt_At_pos, vTerm.Command_Seq);

                                    }

                                    vTermSessionSetValue(vTerm.Command_Sent_Cursor_Pos, vTerm_Expected_Input_Cursor_At_pos, vTerm.Command_Seq);
                                }
                            }
                        }
                    }

                    l_command = true;

                    append_char(vTerm.Command_Processed, PuttyData[l_pos], MAX_BUFFER_SIZE);

                    vTerm.Command_Processed_Len = strlen(vTerm.Command_Processed);

                    if (vTerm.Command_Processing_Started == true) {
                        append_string(vTerm.Command_Processed_ASCII, dupprintf("%d", ','), MAX_BUFFER_SIZE);
                    }
                    else {
                        append_string(vTerm.Command_Processed_ASCII, dupprintf("%d", PuttyData[l_pos]), MAX_BUFFER_SIZE);
                    }

                    vTerm.Command_Processing_Started = true;

                    vTerm.Command_Processed_Logging = true;

                    l_proc = false;

                    l_submit = false;

                    if (vTerm.Command_Seq > vTerm.Command_Seq_Max) {
                        l_proc = true;
                    }
                    else if (vTerm.Command_Sent_Len + vTerm.Submit_Key_Len <= 0) {
                        l_proc = true;
                    }
                    else if (vTerm.Submit_Key_Len <= 0 && strcmp(vTerm.Command_Processed, vTerm.Command_Sent) == 0) {
                        l_proc = true;
                    }
                    else if (vTerm.Submit_Key_Len > 0) {

                        if (l_pos >= vTerm.Submit_Key_Len - 1) {

                            if (strcmp(&PuttyData[l_pos - vTerm.Submit_Key_Len + 1], vTerm.Submit_Key_ANSI) == 0) {

                                l_proc = true;

                                l_submit = true;
                            }
                        }
                        else if (strcmp(vTerm.Command_Processed, vTerm.Submit_Key_ANSI) == 0) {

                            l_proc = true;

                            l_submit = true;
                        }
                    }

                    if (l_proc == true) {

                        l_proc = false;

                        if (l_submit == true || vTerm.Command_Seq > vTerm.Command_Seq_Max) {
                            l_proc = true;
                        }
                        else if (vTerm.Command_Sent_Len > 0 && strcmp(vTerm.Command_Processed, vTerm.Command_Sent) == 0) {

                            if (!(vTerm.Screen_Cursor_Prev_X == vTerm.Screen_Cursor.X - vTerm.Command_Send_Buffer_Len && vTerm.Screen_Cursor_Prev_Y == vTerm.Screen_Cursor.Y)) {
                                l_proc = true;
                            }
                        }
                        else if ((vTerm.Command_Sent_Len <= 0 && l_pos >= DataLength - 1) && (vTerm.Command_Processing == true)) {
                            l_proc = true;
                        }

                        if (l_proc == true) {

                            strcpy(l_cmd, vTerm.Command_Processed);

                            l_cmd_len = vTerm.Command_Processed_Len;

                            l_ptr = -1;

                            char* pch;
                            
                            pch = strrchr(l_cmd, 0x1B);

                            if (pch) l_ptr = pch - l_cmd;

                            if (l_ptr >= 0) {

                                if (vTerm.Command_Seq > vTerm.Command_Seq_Max) {

                                    vTermSubmitKey(&PuttyData[l_pos], true);

                                    l_cmd_len = DataLength;

                                    l_pos = DataLength - 1;
                                }
                                else {
                                    vTermSubmitKey(&l_cmd[l_ptr], true);
                                }
                            }
                            else {
                                vTermSubmitKey(&PuttyData[l_pos], true);
                            }

                            vTermSessionSetValue(vTerm.Command_Processed_ASCII, vTerm_Command_Processed_ASCII_pos, vTerm.Command_Seq);

                            if (vTerm.Command_Submit_Key == true) {

                                l_cmd_len = strlen(l_cmd) - vTerm.Command_Processed_Submit_Key_Len;

                                memset(vTerm.Command_Processed, 0, MAX_BUFFER_SIZE);

                                if (l_cmd_len > 0) {
                                    strncpy(vTerm.Command_Processed, l_cmd, l_cmd_len);
                                }

                                vTermSessionSetValue(vTerm.Command_Processed_Submit_Key, vTerm_Command_Processed_Submit_Key_pos, vTerm.Command_Seq);

                                if (vTerm.Command_Seq > vTerm.Command_Seq_Max) {
                                    vTermSessionSetValue(vTerm.Command_Processed_Submit_Key, vTerm_Command_Submit_Key_pos, vTerm.Command_Seq);
                                }

                                l_submit_key = true;
                            }
                            else if (vTerm.Submit_Key_Len <= 0 && vTerm.Command_Processed_Len == vTerm.Command_Send_Len) {
                                l_submit_key = true;
                            }

                            if (l_submit_key == true || vTerm.Command_Seq > vTerm.Command_Seq_Max) {

                                vTermSessionSetValue(vTerm.Command_Processed, vTerm_Command_Processed_pos, vTerm.Command_Seq);

                                if (l_submit_key == true) {
                                    vTermSetCommandProcessed();
                                }
                                else if (vTerm.Command_Seq > vTerm.Command_Seq_Max) {
                                    vTermSessionSetValue(vTerm.Command_Processed, vTerm_Command_Send_pos, vTerm.Command_Seq);
                                }
                            }

                            if (vTerm.Command_Seq <= vTerm.Command_Seq_Max) {

                                vTerm.Command_Processed_Manual_Input[0] = '\0';

                                vTerm.Command_Processed_Len = 0;
                                vTerm.Command_Processing_Started = false;
                                vTerm.Command_Processing_Finished = true;
                            }

                            vTerm.Command_Send_Buffer_Len = 0;
                            vTerm.Command_Send_Pos = 0;

                            vTerm.Submit_Key_Len = 0;

                            vTerm.Command_Sent[0] = '\0';

                            vTerm.Command_Sent_Len = 0;
                            vTerm.Command_Processing = false;
                        }
                    }
                }
                else if (CommandType == vTerm_Data) {

                    if (CaptureScreensData == true) {

                        append_char(vTerm.Screen_Raw, PuttyData[l_pos], MAX_RAWDATA_LEN);

                        if (strlen(trim(vTerm.Screen_Raw_ASCII)) > 0) {
                            append_char(vTerm.Screen_Raw_ASCII, ',', MAX_RAWDATA_LEN);
                        }

                        append_string(vTerm.Screen_Raw_ASCII, dupprintf("%d", PuttyData[l_pos]), MAX_RAWDATA_LEN);
                    }
                }
            }
        }

        if (CommandType == vTerm_Data && vTerm.Command_Processing == true) {
            vTermSendCommand();
        }
    }

    if (vTermLog_Execution == true) {
        vTermWriteToLog("vTermProcessData|Finish", dupprintf("%s", PuttyData), NULL);
    }
}

void vTermNextScreenRow(bool Screen_Changed) {

    if (vTermLog_Execution == true) {
        vTermWriteToLog(dupprintf("vTermNextScreenRow (%s)|Start", Screen_Changed ? "true" : "false"), vTerm.Screen_New, vTerm.Screen);
    }

    if (Screen_Changed == true) {

        if (vTerm.Command_Seq > vTerm.Screen_Command_Seq_From) {
            vTerm.Screen_Command_Seq_To = vTerm.Command_Seq - 1;
        }
        else {
            vTerm.Screen_Command_Seq_To = vTerm.Command_Seq;
        }

        if (vTerm.Screen_Command_Seq_From <= vTerm.Screen_Command_Seq_To && !(vTerm.Screen_Command_Seq_From == vTerm.Command_Seq)) {
            vTermWriteSessionToFile();
        }

        vTerm.Commands_Input[0] = '\0';
        vTerm.Commands_Processed[0] = '\0';

        vTerm.Screen_Raw_ASCII[0] = '\0';
        vTerm.Screen_Raw[0] = '\0';

        vTerm.Screen_Ptr = -1;
    }
    else if (vTerm.Command_Seq > vTerm.Screen_Command_Seq_From) {

        if (vTerm.Command_Seq <= vTerm.Command_Seq_Max) {
            vTerm.Screen_Command_Seq_To = vTerm.Command_Seq;
        }
    }
    else {
        return;
    }

    if (Screen_Changed == true) {

        vTerm.Screen_Command_Seq_From = vTerm.Command_Seq;

        vTerm.Controller_Updated_Seq = -1;
    }

    if (vTermLog_Execution == true) {
        vTermWriteToLog("vTermNextScreenRow|Finish", vTerm.Screen_New, vTerm.Screen);
    }
}

void vTermScreenUpdated( char* PuttyData, int DataLength) {

    int l_pos;
    int l_ptr;
    int l_ptr2;
    bool l_new;
    bool l_proc;
    int l_rows[3];

    if (vTerm.Hwnd > 0L) {

        if (vTermLog_Execution == true) {
            vTermWriteToLog( "vTermScreenUpdated|Start", PuttyData, vTerm.Screen);
        }

        l_pos = strlen(vTerm.Screen);

        l_new = false;
        l_proc = false;

        l_rows[0] = -1;
        l_rows[1] = -1;

        memset(vTerm.Screen_New, 0, sizeof(vTerm.Screen_New));

        strncpy(vTerm.Screen_New, PuttyData, DataLength);

        vTerm.Screen_New[DataLength] = '\0';
        vTerm.Screen_New_Len = strlen(vTerm.Screen_New);

        if (vTerm.Screen_New_Len <= 0) return;

        if (strcmp(vTerm.Screen_Capture, "Yes") == 0) {

            l_proc = true;

            vTerm.Screen_Capture[0] = '\0';
        }
        else if (l_pos <= 0 || vTerm.Screen_New_Len <= 0) {
            /* Ignore. */
        }
        else if (!(vTerm.Command_Screen_Identifier_Len > 0 && strstr(vTerm.Screen_New, vTerm.Command_Screen_Identifier) != NULL) ||
                 !(vTerm.Command_Prompt_Len > 0 && strstr(vTerm.Screen_New, vTerm.Command_Prompt) != NULL)) {

            l_ptr = string_split((char*)vTerm.Screen_New, '\n', MAX_SCREEN_ROWS, MAX_SCREEN_COLS);

            vTerm.Screen_New_Rows = l_ptr;

            if (vTerm.Screen_Array_Rows > 0 && vTerm.Screen_New_Rows > 0) {

                if (l_ptr > 0) {

                    l_rows[0] = MAX_BUFFER_SIZE - 1;

                    l_ptr = l_rows[0];
                    l_proc = false;

                    /* Last populated row of .Screen_Array. */
                    while (l_ptr > 0 && l_proc == false) {

                        if (strlen(trim(vTerm.Screen_Array[l_ptr])) > 1) {
                            l_proc = true;
                        }
                        else {
                            l_ptr = l_ptr - 1;
                        }
                    }

                    l_rows[1] = MAX_SCREEN_ROWS - 1;

                    l_ptr2 = l_rows[1];

                    l_proc = false;

                    if (l_ptr2 <= 0) {

                        if (l_ptr > 0) {
                            l_proc = true;
                        }
                    }
                    else {

                        /* Latest populated row of String_Array. */
                        while (l_ptr2 > 0 && l_proc == false) {

                            if (strlen(trim(String_Array[l_ptr2])) > 1) {
                                l_proc = true;
                            }
                            else {
                                l_ptr2 = l_ptr2 - 1;
                            }
                        }

                        /* Find start of overlapping content, working backwards from latest rows in both arrays. */
                        while (l_ptr >= 0 && l_ptr2 >= 0 && l_proc == true) {

                            if (strlen(trim(vTerm.Screen_Array[l_ptr])) > 1 && strstr(String_Array[l_ptr2], vTerm.Screen_Array[l_ptr]) != NULL) {
                                /* Found overlapping row. */
                                l_proc = false;
                            }
                            else {
                                l_ptr2 = l_ptr2 - 1;
                            }
                        }

                        /* Find first rows where content does not match. */
                        while (l_ptr >= 0 && l_ptr2 >= 0 && l_proc == false) {

                            if (strlen(trim(vTerm.Screen_Array[l_ptr])) > 0 && strstr(String_Array[l_ptr2], vTerm.Screen_Array[l_ptr]) == NULL) {

                                if (l_ptr2 <= 0) {
                                    l_proc = true;
                                }
                                else if (strcmp(String_Array[l_ptr2], String_Array[l_ptr2 - 1]) == 0) {
                                    l_ptr = l_ptr + 1;
                                }
                                else {
                                    l_proc = true;
                                }
                            }

                            l_ptr = l_ptr - 1;
                            l_ptr2 = l_ptr2 - 1;
                        }
                    }
                }
            }
            else {
                l_proc = true;
            }
        }

        if (vTerm.Command_Processing_Started == true || vTerm.Command_Processed_Logging == true) {

            if (l_proc == true && vTerm.Command_Seq > 1) {

                if (strcmp(vTerm.Screen_New, vTerm.Screen) == 0 && vTerm.Screen_Cursor_Prev_X == vTerm.Screen_Cursor.X && vTerm.Screen_Cursor_Prev_Y == vTerm.Screen_Cursor.Y) {
                    /* Ignore. */
                }
                else if (vTerm.Command_Processed_Len > 1 && vTerm.Command_Processed_Submit_Key_Len <= 0) {
                    /* Ignore. */
                }
                else if (!(vTerm.Screen_Cursor_Prev_X + 1 == vTerm.Screen_Cursor.X && vTerm.Screen_Cursor_Prev_Y == vTerm.Screen_Cursor.Y)) {

                    if (vTerm.Submit_Key_Len <= 0 && vTerm.Command_Processed_Len == vTerm.Command_Send_Len) {

                        vTermSetCommandProcessed();

                        if (vTerm.Command_Processing_Started == true) vTerm.Command_Processing_Finished = true;
                    }
                }
            }
        }

        if (vTerm.Command_Processing_Finished == true) {

            vTerm.Command_Mismatch = false;

            vTerm.Command_Processing = false;
            vTerm.Command_Processing_Started = false;
            vTerm.Command_Processing_Finished = false;

            vTerm.Submit_Key[0] = '\0';
            vTerm.Command_Processed[0] = '\0';

            vTerm.Command_Seq = vTerm.Command_Seq + 1;

            if ((vTerm.Command_Seq > 1) && (vTerm.Command_Seq == vTerm.Command_Seq_Max + 1)) {

                RecordForScripting = true;

                vTerm.Screen_Command_Seq_To = vTerm.Command_Seq - 1;

                vTermNextScreenRow(false);

            }

            l_new = true;

            vTerm.Row_Updated_At = time(NULL);
            vTerm.Screen_Get = false;

            vTermSetCommand();
        }

        if (l_proc == true) {

            vTermNextScreenRow(true);

            memset(vTerm.Screen, 0, sizeof(vTerm.Screen));

            strcpy(vTerm.Screen, vTerm.Screen_New);
        }

        else if (vTerm.Command_Seq > vTerm.Command_Seq_Max) {

            memset(vTerm.Screen, 0, sizeof(vTerm.Screen));

            strcpy(vTerm.Screen, vTerm.Screen_New);

        }

        else if (l_rows[0] < 0 || l_rows[1] < 0) {

            memset(vTerm.Screen, 0, sizeof(vTerm.Screen));

            strcpy(vTerm.Screen, vTerm.Screen_New);

        }

        else {

            l_proc = true;

            l_pos = 0;

            l_ptr = MAX_SCREEN_ROWS - 1;
            l_ptr2 = MAX_SCREEN_ROWS - 1;

            while (l_pos <= l_rows[0] && l_proc == true) {

                if (l_pos > l_rows[1]) {
                    l_pos = l_rows[0] + 99;
                }

                else if (strlen(trim(vTerm.Screen_Array[l_pos])) > 1 && strstr(String_Array[l_pos], vTerm.Screen_Array[l_pos]) == NULL) {
                    l_proc = false;
                }

                l_pos = l_pos + 1;
            }

            if (l_proc == true) {

                memset(vTerm.Screen, 0, sizeof(vTerm.Screen));

                strcpy(vTerm.Screen, vTerm.Screen_New);
            }

            else {

                memset(vTerm.Screen, 0, sizeof(vTerm.Screen));

                vTerm.Screen[0] = '\0';

                for (l_ptr = 0; l_ptr <= l_pos - 1; l_ptr++) {

                    for (l_ptr2 = 0; l_ptr2 < strlen(vTerm.Screen_Array[l_ptr]); l_ptr2++) {
                        append_char(vTerm.Screen, vTerm.Screen_Array[l_ptr][l_ptr2], MAX_SCREEN_ROWS * MAX_SCREEN_COLS);
                    }

                    append_char(vTerm.Screen, '\n', MAX_BUFFER_SIZE);
                }

                append_string(vTerm.Screen, vTerm.Screen_New, MAX_BUFFER_SIZE);
            }
        }

        vTerm.Screen_Len = vTerm.Screen_New_Len;

        memcpy(vTerm.Screen_Array, String_Array, sizeof(String_Array));

        vTerm.Screen_Array_Rows = vTerm.Screen_New_Rows;

        vTermSessionSetValue( vTerm.Screen, vTerm_Screen_pos, vTerm.Screen_Command_Seq_From, vTerm.Command_Seq);
        vTermSessionSetValue( vTerm.Screen_Command_Seq_From, vTerm_Screen_Command_Seq_pos, vTerm.Command_Seq);

        if (!(l_new == true) && !(l_proc == true) && vTerm.Command_Seq > vTerm.Command_Seq_Max) {
            vTermNextScreenRow( false);
        }

        vTermSendCommand();

        vTerm.Command_Submit_Key = false;

        vTerm.Screen_Cursor_Prev_X = vTerm.Screen_Cursor.X;
        vTerm.Screen_Cursor_Prev_Y = vTerm.Screen_Cursor.Y;

        vTerm.Screen_Get = false;

        if (vTermLog_Execution == true) {
            vTermWriteToLog( "vTermScreenUpdated|Finish", vTerm.Screen_New, vTerm.Screen);
        }
    }
}

void vTermInitialise(long term_hwnd) {

    vTermSessionTimeStamp();

    DBDelimiter = '|';

    CaptureScreensData = true;

    RecordForScripting = false;

    SessionsKeyPressSync = false;

    if (!(vterm_nolog == true)) {

        if (strlen(vterm_log_file) == 0) {

            if (vterm_sessionid > 0) {
                strcpy(vterm_log_file, dupstr(vTermSetFileName("Logs", dupprintf("%s_%d", vterm_hostname, vterm_sessionid), "log", true, true)));
            }
            else {
                strcpy(vterm_log_file, dupstr(vTermSetFileName("Logs", dupprintf("%s", vterm_hostname), "log", true, true)));
            }
        }

        vTermInitialiseLogs();
    }

    if (!(vterm_nocapture == true)) {

        if (strlen(vterm_capture_file) == 0) {

            if (vterm_sessionid > 0) {
                strcpy(vterm_capture_file, dupstr(vTermSetFileName("Capture", dupprintf("%s_%d_outputs", vterm_hostname, vterm_sessionid), "log", true, true)));
            }
            else {
                strcpy(vterm_capture_file, dupstr(vTermSetFileName("Capture", dupprintf("%s_outputs", vterm_hostname), "log", true, true)));
            }
        }

        if (stricmp(vterm_inputs_file, "yes") == 0 ||
            stricmp(vterm_inputs_file, "on") == 0) {

            if (vterm_sessionid > 0) {
                strcpy(vterm_inputs_file, dupstr(vTermSetFileName("Capture", dupprintf("%s_%d_inputs", vterm_hostname, vterm_sessionid), "log", true, true)));
            }
            else {
                strcpy(vterm_inputs_file, dupstr(vTermSetFileName("Capture", dupprintf("%s_inputs", vterm_hostname), "log", true, true)));
            }

        }
    }

    vTermLog_Execution = true;

    ReadKeyCodesFromFile();

    ReadCommandsFromFile();

    vTermSessionInitialise(vterm_sessionid);

    vTerm.Pid = (int)getpid();

    vTerm.Hwnd = term_hwnd;

    vTermSetCommand();

    if (strlen(vterm_capture_file) >= 0) {
        vTermOpenSessionFiles();
    }
}

