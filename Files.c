/*
 * ============================================================
 * FILES - Storage Memory File Manager
 *
 * Target:
 *   Casio fx-9750GIII / fx-9860G family
 *
 * SDK:
 *   Legacy Casio fx-9860G SDK
 *   Hitachi SH C compiler
 *
 * STORAGE:
 *   Storage Memory only
 *   \\fls0
 *
 * Features:
 *   - Directory browser
 *   - Open text files
 *   - File information
 *   - Rename files
 *   - Rename directories
 *   - Copy files
 *   - Copy directories
 *   - Move files
 *   - Move directories
 *   - Delete files
 *   - Delete directories
 *   - Create directory
 *   - Free storage space
 *
 * Controls:
 *   UP/DOWN   Select
 *   EXE       Open / View
 *   EXIT      Parent / Exit
 *   F1        Info
 *   F2        Operations
 *   MENU      Storage information
 *
 * ============================================================
 */

#include "fxlib.h"
#include "filebios.h"

#include <string.h>
#include <stdio.h>


/* ============================================================
 * CONFIG
 * ============================================================
 */

#define MAX_ITEMS       64
#define NAME_SIZE       32
#define PATH_SIZE       128
#define COPY_BUFFER     512
#define VISIBLE_ROWS    5

#define ITEM_FILE       0
#define ITEM_DIRECTORY  1


/* ============================================================
 * KEYS
 * ============================================================
 */

#ifndef KEY_CTRL_EXE
#define KEY_CTRL_EXE    31
#endif

#ifndef KEY_CTRL_UP
#define KEY_CTRL_UP     28
#endif

#ifndef KEY_CTRL_DOWN
#define KEY_CTRL_DOWN   37
#endif

#ifndef KEY_CTRL_LEFT
#define KEY_CTRL_LEFT   38
#endif

#ifndef KEY_CTRL_RIGHT
#define KEY_CTRL_RIGHT  27
#endif

#ifndef KEY_CTRL_EXIT
#define KEY_CTRL_EXIT   47
#endif

#ifndef KEY_CTRL_DEL
#define KEY_CTRL_DEL    44
#endif

#ifndef KEY_CTRL_F1
#define KEY_CTRL_F1     79
#endif

#ifndef KEY_CTRL_F2
#define KEY_CTRL_F2     69
#endif

#ifndef KEY_CTRL_MENU
#define KEY_CTRL_MENU   48
#endif


/* ============================================================
 * ENTRY
 * ============================================================
 */

typedef struct
{
    char name[NAME_SIZE];
    unsigned long size;
    int type;
} FM_ENTRY;


/* ============================================================
 * GLOBALS
 * ============================================================
 */

static FM_ENTRY entries[MAX_ITEMS];

static int entry_count;
static int selected;
static int first_visible;

static char current_path[PATH_SIZE];


/* ============================================================
 * DISPLAY
 * ============================================================
 */

static void fm_clear(void)
{
    Bdisp_AllClr_DDVRAM();
}


static void fm_print(int x, int y, const char *s)
{
    PrintXY(
        x,
        y,
        (const unsigned char *)s,
        0
    );
}


static void fm_print_rev(int x, int y, const char *s)
{
    PrintXY(
        x,
        y,
        (const unsigned char *)s,
        1
    );
}


static void fm_mini(int x, int y, const char *s)
{
    PrintMini(
        x,
        y,
        (const unsigned char *)s,
        MINI_OVER
    );
}


static void fm_number(int x, int y, long n)
{
    char s[20];

    sprintf(s, "%ld", n);

    fm_mini(x, y, s);
}


static void fm_present(void)
{
    /*
     * IMPORTANT:
     * Legacy SDK uses Bdisp_PutDisp_DD().
     *
     * Do NOT use:
     * Bdisp_PutDisp_DDVRAM()
     */

    Bdisp_PutDisp_DD();
}


/* ============================================================
 * WAIT
 * ============================================================
 */

static void fm_wait(void)
{
    unsigned int key;

    while (1)
    {
        GetKey(&key);

        if (
            key == KEY_CTRL_EXE ||
            key == KEY_CTRL_EXIT
        )
        {
            return;
        }
    }
}


/* ============================================================
 * MESSAGE
 * ============================================================
 */

static void fm_message(
    const char *title,
    const char *message
)
{
    fm_clear();

    fm_print(1, 1, title);

    Bdisp_DrawLineVRAM(
        0,
        10,
        127,
        10
    );

    fm_print(1, 23, message);

    fm_print(1, 55, "EXE / EXIT");

    fm_present();

    fm_wait();
}


/* ============================================================
 * FONTCHARACTER CONVERSION
 * ============================================================
 */

static void fm_to_font(
    const char *src,
    FONTCHARACTER *dst
)
{
    int i;

    i = 0;

    while (
        src[i] != 0 &&
        i < PATH_SIZE - 1
    )
    {
        dst[i] = (FONTCHARACTER)src[i];
        i++;
    }

    dst[i] = 0;
}


static void fm_from_font(
    const FONTCHARACTER *src,
    char *dst,
    int max
)
{
    int i;

    i = 0;

    while (
        src[i] != 0 &&
        i < max - 1
    )
    {
        if ((unsigned short)src[i] < 128)
        {
            dst[i] = (char)src[i];
        }
        else
        {
            dst[i] = '?';
        }

        i++;
    }

    dst[i] = 0;
}


/* ============================================================
 * ROOT
 * ============================================================
 */

static void fm_root(void)
{
    strcpy(
        current_path,
        "\\\\fls0"
    );
}


/* ============================================================
 * BUILD PATH
 * ============================================================
 */

static void fm_make_path(
    const char *name,
    char *out
)
{
    strcpy(
        out,
        current_path
    );

    if (
        out[strlen(out) - 1] != '\\'
    )
    {
        strcat(
            out,
            "\\"
        );
    }

    strcat(
        out,
        name
    );
}


/* ============================================================
 * PARENT DIRECTORY
 * ============================================================
 */

static void fm_parent(void)
{
    int i;
    int len;

    len = strlen(current_path);

    /*
     * Already at root.
     */
    if (
        strcmp(
            current_path,
            "\\\\fls0"
        ) == 0
    )
    {
        return;
    }

    i = len - 1;

    while (
        i > 5 &&
        current_path[i] != '\\'
    )
    {
        i--;
    }

    if (i > 5)
    {
        current_path[i] = 0;
    }
    else
    {
        fm_root();
    }
}


/* ============================================================
 * LOAD DIRECTORY
 * ============================================================
 */

static int fm_load(void)
{
    FONTCHARACTER search[PATH_SIZE];
    FONTCHARACTER found[NAME_SIZE];

    FILE_INFO info;

    char search_path[PATH_SIZE];

    int handle;
    int result;


    entry_count = 0;
    selected = 0;
    first_visible = 0;


    strcpy(
        search_path,
        current_path
    );

    strcat(
        search_path,
        "\\*"
    );


    fm_to_font(
        search_path,
        search
    );


    result =
        Bfile_FindFirst(
            search,
            &handle,
            found,
            &info
        );


    if (result < 0)
    {
        return result;
    }


    while (
        result == 0 &&
        entry_count < MAX_ITEMS
    )
    {
        fm_from_font(
            found,
            entries[entry_count].name,
            NAME_SIZE
        );


        entries[entry_count].size =
            info.fsize;


        if (
            info.type == DT_DIRECTORY
        )
        {
            entries[entry_count].type =
                ITEM_DIRECTORY;
        }
        else
        {
            entries[entry_count].type =
                ITEM_FILE;
        }


        entry_count++;


        result =
            Bfile_FindNext(
                handle,
                found,
                &info
            );
    }


    Bfile_FindClose(
        handle
    );


    return 0;
}


/* ============================================================
 * SELECTION
 * ============================================================
 */

static void fm_up(void)
{
    if (entry_count <= 0)
    {
        return;
    }

    selected--;

    if (selected < 0)
    {
        selected = entry_count - 1;
    }

    if (
        selected < first_visible
    )
    {
        first_visible = selected;
    }
}


static void fm_down(void)
{
    if (entry_count <= 0)
    {
        return;
    }

    selected++;

    if (
        selected >= entry_count
    )
    {
        selected = 0;
    }

    if (
        selected >= first_visible + VISIBLE_ROWS
    )
    {
        first_visible =
            selected - VISIBLE_ROWS + 1;
    }
}


/* ============================================================
 * DRAW BROWSER
 * ============================================================
 */

static void fm_draw_browser(void)
{
    int i;
    int index;
    int length;

    char path_display[24];


    fm_clear();


    fm_print(
        1,
        1,
        "[FILES]"
    );


    length =
        strlen(current_path);


    if (length > 20)
    {
        strcpy(
            path_display,
            current_path + length - 20
        );
    }
    else
    {
        strcpy(
            path_display,
            current_path
        );
    }


    fm_mini(
        1,
        9,
        path_display
    );


    Bdisp_DrawLineVRAM(
        0,
        16,
        127,
        16
    );


    for (
        i = 0;
        i < VISIBLE_ROWS;
        i++
    )
    {
        char line[24];

        index =
            first_visible + i;


        if (
            index >= entry_count
        )
        {
            continue;
        }


        line[0] = 0;


        if (
            entries[index].type ==
            ITEM_DIRECTORY
        )
        {
            strcpy(
                line,
                "[D] "
            );
        }


        strncat(
            line,
            entries[index].name,
            20
        );


        if (
            index == selected
        )
        {
            Bdisp_AreaReverseVRAM(
                0,
                18 + i * 8,
                127,
                25 + i * 8
            );
        }


        fm_print(
            1,
            18 + i * 8,
            line
        );
    }


    fm_mini(
        1,
        57,
        "F1 INFO F2 OPS MENU"
    );


    fm_present();
}


/* ============================================================
 * CONFIRM
 * ============================================================
 */

static int fm_confirm(
    const char *title,
    const char *message
)
{
    unsigned int key;


    fm_clear();


    fm_print(
        1,
        1,
        title
    );


    Bdisp_DrawLineVRAM(
        0,
        10,
        127,
        10
    );


    fm_print(
        1,
        22,
        message
    );


    fm_print(
        1,
        40,
        "EXE YES"
    );


    fm_print(
        1,
        51,
        "EXIT NO"
    );


    fm_present();


    while (1)
    {
        GetKey(&key);

        if (
            key == KEY_CTRL_EXE
        )
        {
            return 1;
        }

        if (
            key == KEY_CTRL_EXIT
        )
        {
            return 0;
        }
    }
}


/* ============================================================
 * SIMPLE NAME INPUT
 *
 * Uses UP/DOWN to cycle characters.
 * ============================================================
 */

static const char fm_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    "_-. ";


static void fm_draw_input(
    const char *title,
    const char *text,
    int cursor
)
{
    fm_clear();

    fm_print(
        1,
        1,
        title
    );

    Bdisp_DrawLineVRAM(
        0,
        10,
        127,
        10
    );

    fm_print(
        1,
        23,
        text
    );

    if (cursor < 20)
    {
        fm_print_rev(
            1 + cursor * 6,
            34,
            "_"
        );
    }

    fm_mini(
        1,
        46,
        "UP/DN CHAR"
    );

    fm_mini(
        1,
        55,
        "EXE OK DEL BACK"
    );

    fm_present();
}


static int fm_input(
    const char *title,
    char *text,
    int max
)
{
    unsigned int key;

    int cursor;
    int length;
    int i;
    int j;
    int count;

    char c;


    length = strlen(text);
    cursor = length;


    while (1)
    {
        fm_draw_input(
            title,
            text,
            cursor
        );


        GetKey(&key);


        /*
         * EXE
         */
        if (
            key == KEY_CTRL_EXE
        )
        {
            if (strlen(text) == 0)
            {
                return 0;
            }

            return 1;
        }


        /*
         * EXIT
         */
        if (
            key == KEY_CTRL_EXIT
        )
        {
            return 0;
        }


        /*
         * DEL
         */
        if (
            key == KEY_CTRL_DEL
        )
        {
            if (cursor > 0)
            {
                for (
                    i = cursor - 1;
                    i < length;
                    i++
                )
                {
                    text[i] =
                        text[i + 1];
                }

                cursor--;
                length--;
            }

            continue;
        }


        /*
         * LEFT
         */
        if (
            key == KEY_CTRL_LEFT
        )
        {
            if (cursor > 0)
            {
                cursor--;
            }

            continue;
        }


        /*
         * RIGHT
         */
        if (
            key == KEY_CTRL_RIGHT
        )
        {
            if (cursor < length)
            {
                cursor++;
            }

            continue;
        }


        /*
         * UP/DOWN
         */
        if (
            key == KEY_CTRL_UP ||
            key == KEY_CTRL_DOWN
        )
        {
            count = strlen(fm_chars);

            if (cursor >= length)
            {
                if (length < max - 1)
                {
                    text[length] =
                        fm_chars[0];

                    text[length + 1] =
                        0;

                    length++;
                }
            }
            else
            {
                j = 0;

                while (
                    j < count &&
                    fm_chars[j] != text[cursor]
                )
                {
                    j++;
                }

                if (j >= count)
                {
                    j = 0;
                }

                if (
                    key == KEY_CTRL_UP
                )
                {
                    j--;

                    if (j < 0)
                    {
                        j = count - 1;
                    }
                }
                else
                {
                    j++;

                    if (j >= count)
                    {
                        j = 0;
                    }
                }

                text[cursor] =
                    fm_chars[j];
            }

            continue;
        }


        /*
         * Normal calculator character keys.
         */
        c = 0;

#ifdef KEY_CHAR_0
        switch (key)
        {
            case KEY_CHAR_0: c = '0'; break;
            case KEY_CHAR_1: c = '1'; break;
            case KEY_CHAR_2: c = '2'; break;
            case KEY_CHAR_3: c = '3'; break;
            case KEY_CHAR_4: c = '4'; break;
            case KEY_CHAR_5: c = '5'; break;
            case KEY_CHAR_6: c = '6'; break;
            case KEY_CHAR_7: c = '7'; break;
            case KEY_CHAR_8: c = '8'; break;
            case KEY_CHAR_9: c = '9'; break;

            case KEY_CHAR_A: c = 'A'; break;
            case KEY_CHAR_B: c = 'B'; break;
            case KEY_CHAR_C: c = 'C'; break;
            case KEY_CHAR_D: c = 'D'; break;
            case KEY_CHAR_E: c = 'E'; break;
            case KEY_CHAR_F: c = 'F'; break;
            case KEY_CHAR_G: c = 'G'; break;
            case KEY_CHAR_H: c = 'H'; break;
            case KEY_CHAR_I: c = 'I'; break;
            case KEY_CHAR_J: c = 'J'; break;
            case KEY_CHAR_K: c = 'K'; break;
            case KEY_CHAR_L: c = 'L'; break;
            case KEY_CHAR_M: c = 'M'; break;
            case KEY_CHAR_N: c = 'N'; break;
            case KEY_CHAR_O: c = 'O'; break;
            case KEY_CHAR_P: c = 'P'; break;
            case KEY_CHAR_Q: c = 'Q'; break;
            case KEY_CHAR_R: c = 'R'; break;
            case KEY_CHAR_S: c = 'S'; break;
            case KEY_CHAR_T: c = 'T'; break;
            case KEY_CHAR_U: c = 'U'; break;
            case KEY_CHAR_V: c = 'V'; break;
            case KEY_CHAR_W: c = 'W'; break;
            case KEY_CHAR_X: c = 'X'; break;
            case KEY_CHAR_Y: c = 'Y'; break;
            case KEY_CHAR_Z: c = 'Z'; break;

#ifdef KEY_CHAR_SPACE
            case KEY_CHAR_SPACE: c = ' '; break;
#endif

            default:
                break;
        }
#endif


        if (c != 0)
        {
            if (cursor < max - 1)
            {
                for (
                    i = length;
                    i > cursor;
                    i--
                )
                {
                    text[i] =
                        text[i - 1];
                }

                text[cursor] = c;

                cursor++;
                length++;

                text[length] = 0;
            }
        }
    }
}


/* ============================================================
 * FILE INFO
 * ============================================================
 */

static void fm_info(void)
{
    char path[PATH_SIZE];

    FONTCHARACTER fpath[PATH_SIZE];

    int handle;
    int size;


    if (entry_count <= 0)
    {
        return;
    }


    fm_make_path(
        entries[selected].name,
        path
    );


    fm_to_font(
        path,
        fpath
    );


    fm_clear();


    fm_print(
        1,
        1,
        "[INFO]"
    );


    Bdisp_DrawLineVRAM(
        0,
        10,
        127,
        10
    );


    fm_print(
        1,
        16,
        entries[selected].name
    );


    if (
        entries[selected].type ==
        ITEM_DIRECTORY
    )
    {
        fm_print(
            1,
            30,
            "TYPE: DIRECTORY"
        );
    }
    else
    {
        fm_print(
            1,
            30,
            "TYPE: FILE"
        );


        handle =
            Bfile_OpenFile(
                fpath,
                _OPENMODE_READ
            );


        if (handle >= 0)
        {
            size =
                Bfile_GetFileSize(
                    handle
                );


            Bfile_CloseFile(
                handle
            );


            if (size >= 0)
            {
                fm_print(
                    1,
                    41,
                    "SIZE:"
                );

                fm_number(
                    35,
                    41,
                    size
                );

                fm_print(
                    78,
                    41,
                    "BYTES"
                );
            }
        }
    }


    fm_print(
        1,
        56,
        "EXE / EXIT"
    );


    fm_present();

    fm_wait();
}


/* ============================================================
 * DELETE FILE
 * ============================================================
 */

static int fm_delete_file(
    const char *path
)
{
    FONTCHARACTER fpath[PATH_SIZE];

    fm_to_font(
        path,
        fpath
    );

    return Bfile_DeleteFile(
        fpath
    );
}


/* ============================================================
 * DELETE DIRECTORY TREE
 * ============================================================
 */

static int fm_delete_tree(
    const char *path
)
{
    FONTCHARACTER search[PATH_SIZE];
    FONTCHARACTER found[NAME_SIZE];

    FILE_INFO info;

    char search_path[PATH_SIZE];
    char child[PATH_SIZE];
    char name[NAME_SIZE];

    int handle;
    int result;


    strcpy(
        search_path,
        path
    );

    strcat(
        search_path,
        "\\*"
    );


    fm_to_font(
        search_path,
        search
    );


    result =
        Bfile_FindFirst(
            search,
            &handle,
            found,
            &info
        );


    if (result == 0)
    {
        while (result == 0)
        {
            fm_from_font(
                found,
                name,
                NAME_SIZE
            );


            if (
                strcmp(name, ".") != 0 &&
                strcmp(name, "..") != 0
            )
            {
                strcpy(
                    child,
                    path
                );

                strcat(
                    child,
                    "\\"
                );

                strcat(
                    child,
                    name
                );


                if (
                    info.type ==
                    DT_DIRECTORY
                )
                {
                    result =
                        fm_delete_tree(
                            child
                        );
                }
                else
                {
                    result =
                        fm_delete_file(
                            child
                        );
                }


                if (result < 0)
                {
                    Bfile_FindClose(
                        handle
                    );

                    return result;
                }
            }


            result =
                Bfile_FindNext(
                    handle,
                    found,
                    &info
                );
        }


        Bfile_FindClose(
            handle
        );
    }


    {
        FONTCHARACTER fpath[PATH_SIZE];

        fm_to_font(
            path,
            fpath
        );

        return Bfile_DeleteDirectory(
            fpath
        );
    }
}


/* ============================================================
 * COPY FILE
 * ============================================================
 */

static int fm_copy_file(
    const char *source,
    const char *destination
)
{
    FONTCHARACTER src_font[PATH_SIZE];
    FONTCHARACTER dst_font[PATH_SIZE];

    unsigned char buffer[COPY_BUFFER];

    int src;
    int dst;
    int size;
    int position;
    int amount;
    int bytes_read;
    int result;


    fm_to_font(
        source,
        src_font
    );

    fm_to_font(
        destination,
        dst_font
    );


    src =
        Bfile_OpenFile(
            src_font,
            _OPENMODE_READ
        );


    if (src < 0)
    {
        return src;
    }


    size =
        Bfile_GetFileSize(
            src
        );


    if (size < 0)
    {
        Bfile_CloseFile(src);
        return -1;
    }


    /*
     * Remove destination if it exists.
     */
    Bfile_DeleteFile(
        dst_font
    );


    result =
        Bfile_CreateFile(
            dst_font,
            size
        );


    if (result < 0)
    {
        Bfile_CloseFile(src);
        return result;
    }


    dst =
        Bfile_OpenFile(
            dst_font,
            _OPENMODE_WRITE
        );


    if (dst < 0)
    {
        Bfile_CloseFile(src);
        return dst;
    }


    position = 0;


    while (
        position < size
    )
    {
        amount =
            size - position;


        if (
            amount > COPY_BUFFER
        )
        {
            amount = COPY_BUFFER;
        }


        bytes_read =
            Bfile_ReadFile(
                src,
                buffer,
                amount,
                position
            );


        if (bytes_read <= 0)
        {
            Bfile_CloseFile(dst);
            Bfile_CloseFile(src);

            return -1;
        }


        result =
            Bfile_WriteFile(
                dst,
                buffer,
                bytes_read
            );


        if (result < 0)
        {
            Bfile_CloseFile(dst);
            Bfile_CloseFile(src);

            return result;
        }


        position += bytes_read;
    }


    Bfile_CloseFile(dst);
    Bfile_CloseFile(src);

    return 0;
}


/* ============================================================
 * COPY DIRECTORY
 * ============================================================
 */

static int fm_copy_tree(
    const char *source,
    const char *destination
)
{
    FONTCHARACTER dest_font[PATH_SIZE];

    FONTCHARACTER search[PATH_SIZE];
    FONTCHARACTER found[NAME_SIZE];

    FILE_INFO info;

    char search_path[PATH_SIZE];
    char src_child[PATH_SIZE];
    char dst_child[PATH_SIZE];
    char name[NAME_SIZE];

    int handle;
    int result;


    fm_to_font(
        destination,
        dest_font
    );


    result =
        Bfile_CreateDirectory(
            dest_font
        );


    if (
        result < 0
    )
    {
        return result;
    }


    strcpy(
        search_path,
        source
    );

    strcat(
        search_path,
        "\\*"
    );


    fm_to_font(
        search_path,
        search
    );


    result =
        Bfile_FindFirst(
            search,
            &handle,
            found,
            &info
        );


    if (
        result < 0
    )
    {
        return result;
    }


    while (
        result == 0
    )
    {
        fm_from_font(
            found,
            name,
            NAME_SIZE
        );


        if (
            strcmp(name, ".") != 0 &&
            strcmp(name, "..") != 0
        )
        {
            strcpy(
                src_child,
                source
            );

            strcat(
                src_child,
                "\\"
            );

            strcat(
                src_child,
                name
            );


            strcpy(
                dst_child,
                destination
            );

            strcat(
                dst_child,
                "\\"
            );

            strcat(
                dst_child,
                name
            );


            if (
                info.type ==
                DT_DIRECTORY
            )
            {
                result =
                    fm_copy_tree(
                        src_child,
                        dst_child
                    );
            }
            else
            {
                result =
                    fm_copy_file(
                        src_child,
                        dst_child
                    );
            }


            if (result < 0)
            {
                Bfile_FindClose(handle);
                return result;
            }
        }


        result =
            Bfile_FindNext(
                handle,
                found,
                &info
            );
    }


    Bfile_FindClose(
        handle
    );


    return 0;
}


/* ============================================================
 * DELETE SELECTED
 * ============================================================
 */

static void fm_delete_selected(void)
{
    char path[PATH_SIZE];

    int result;


    if (entry_count <= 0)
    {
        return;
    }


    if (
        !fm_confirm(
            "[DELETE]",
            entries[selected].name
        )
    )
    {
        return;
    }


    fm_make_path(
        entries[selected].name,
        path
    );


    if (
        entries[selected].type ==
        ITEM_DIRECTORY
    )
    {
        result =
            fm_delete_tree(
                path
            );
    }
    else
    {
        result =
            fm_delete_file(
                path
            );
    }


    if (result < 0)
    {
        fm_message(
            "[ERROR]",
            "DELETE FAILED"
        );
    }
    else
    {
        fm_message(
            "[FILES]",
            "DELETE COMPLETE"
        );
    }


    fm_load();
}


/* ============================================================
 * NEW DIRECTORY
 * ============================================================
 */

static void fm_new_directory(void)
{
    char name[NAME_SIZE];
    char path[PATH_SIZE];

    FONTCHARACTER fpath[PATH_SIZE];

    int result;


    strcpy(
        name,
        "NEWFOLDER"
    );


    if (
        !fm_input(
            "[NEW FOLDER]",
            name,
            NAME_SIZE
        )
    )
    {
        return;
    }


    if (
        strchr(name, '\\') != 0 ||
        strchr(name, '/') != 0
    )
    {
        fm_message(
            "[ERROR]",
            "INVALID NAME"
        );

        return;
    }


    strcpy(
        path,
        current_path
    );

    strcat(
        path,
        "\\"
    );

    strcat(
        path,
        name
    );


    fm_to_font(
        path,
        fpath
    );


    result =
        Bfile_CreateDirectory(
            fpath
        );


    if (result < 0)
    {
        fm_message(
            "[ERROR]",
            "CREATE FAILED"
        );
    }
    else
    {
        fm_message(
            "[FILES]",
            "FOLDER CREATED"
        );
    }


    fm_load();
}


/* ============================================================
 * RENAME
 *
 * Storage Memory rename is implemented using:
 *
 *     copy -> delete
 *
 * ============================================================
 */

static void fm_rename(void)
{
    char old_path[PATH_SIZE];
    char new_path[PATH_SIZE];
    char new_name[NAME_SIZE];

    int result;


    if (entry_count <= 0)
    {
        return;
    }


    strcpy(
        new_name,
        entries[selected].name
    );


    if (
        !fm_input(
            "[RENAME]",
            new_name,
            NAME_SIZE
        )
    )
    {
        return;
    }


    if (
        strchr(new_name, '\\') != 0 ||
        strchr(new_name, '/') != 0
    )
    {
        fm_message(
            "[ERROR]",
            "INVALID NAME"
        );

        return;
    }


    if (
        strcmp(
            new_name,
            entries[selected].name
        ) == 0
    )
    {
        return;
    }


    fm_make_path(
        entries[selected].name,
        old_path
    );


    strcpy(
        new_path,
        current_path
    );

    strcat(
        new_path,
        "\\"
    );

    strcat(
        new_path,
        new_name
    );


    if (
        entries[selected].type ==
        ITEM_DIRECTORY
    )
    {
        result =
            fm_copy_tree(
                old_path,
                new_path
            );


        if (result >= 0)
        {
            result =
                fm_delete_tree(
                    old_path
                );
        }
    }
    else
    {
        result =
            fm_copy_file(
                old_path,
                new_path
            );


        if (result >= 0)
        {
            result =
                fm_delete_file(
                    old_path
                );
        }
    }


    if (result < 0)
    {
        fm_message(
            "[ERROR]",
            "RENAME FAILED"
        );
    }
    else
    {
        fm_message(
            "[FILES]",
            "RENAME COMPLETE"
        );
    }


    fm_load();
}


/* ============================================================
 * DIRECTORY PICKER
 * ============================================================
 */

static int fm_directory_picker(
    char *result_path
)
{
    char saved_path[PATH_SIZE];

    unsigned int key;

    int old_selected;
    int old_first;

    int dirs[MAX_ITEMS];
    int dir_count;

    int dir_selected;
    int i;


    strcpy(
        saved_path,
        current_path
    );


    old_selected =
        selected;

    old_first =
        first_visible;


    fm_root();


    while (1)
    {
        fm_load();


        dir_count = 0;


        for (
            i = 0;
            i < entry_count &&
            dir_count < MAX_ITEMS;
            i++
        )
        {
            if (
                entries[i].type ==
                ITEM_DIRECTORY
            )
            {
                if (
                    strcmp(
                        entries[i].name,
                        "."
                    ) != 0 &&
                    strcmp(
                        entries[i].name,
                        ".."
                    ) != 0
                )
                {
                    dirs[dir_count] = i;
                    dir_count++;
                }
            }
        }


        if (dir_count == 0)
        {
            strcpy(
                result_path,
                current_path
            );

            strcpy(
                current_path,
                saved_path
            );

            selected =
                old_selected;

            first_visible =
                old_first;

            return 1;
        }


        dir_selected = 0;


        while (1)
        {
            fm_clear();


            fm_print(
                1,
                1,
                "[SELECT FOLDER]"
            );


            fm_mini(
                1,
                9,
                current_path
            );


            Bdisp_DrawLineVRAM(
                0,
                16,
                127,
                16
            );


            for (
                i = 0;
                i < dir_count &&
                i < 5;
                i++
            )
            {
                if (
                    i == dir_selected
                )
                {
                    Bdisp_AreaReverseVRAM(
                        0,
                        18 + i * 8,
                        127,
                        25 + i * 8
                    );
                }


                fm_print(
                    1,
                    18 + i * 8,
                    entries[dirs[i]].name
                );
            }


            fm_mini(
                1,
                57,
                "EXE ENTER EXIT OK"
            );


            fm_present();


            GetKey(&key);


            if (
                key == KEY_CTRL_UP
            )
            {
                dir_selected--;

                if (dir_selected < 0)
                {
                    dir_selected =
                        dir_count - 1;

                    if (dir_selected >= 5)
                    {
                        dir_selected = 4;
                    }
                }
            }


            else if (
                key == KEY_CTRL_DOWN
            )
            {
                dir_selected++;

                if (
                    dir_selected >= dir_count ||
                    dir_selected >= 5
                )
                {
                    dir_selected = 0;
                }
            }


            else if (
                key == KEY_CTRL_EXE
            )
            {
                if (
                    strlen(current_path) +
                    strlen(
                        entries[
                            dirs[dir_selected]
                        ].name
                    ) +
                    2 <
                    PATH_SIZE
                )
                {
                    strcat(
                        current_path,
                        "\\"
                    );

                    strcat(
                        current_path,
                        entries[
                            dirs[dir_selected]
                        ].name
                    );
                }

                break;
            }


            else if (
                key == KEY_CTRL_EXIT
            )
            {
                strcpy(
                    current_path,
                    saved_path
                );

                selected =
                    old_selected;

                first_visible =
                    old_first;

                return 0;
            }
        }
    }
}


/* ============================================================
 * COPY / MOVE
 * ============================================================
 */

static void fm_copy_move(
    int move
)
{
    char destination[PATH_SIZE];
    char source[PATH_SIZE];
    char destination_path[PATH_SIZE];

    int result;


    if (entry_count <= 0)
    {
        return;
    }


    if (
        !fm_directory_picker(
            destination
        )
    )
    {
        return;
    }


    fm_make_path(
        entries[selected].name,
        source
    );


    strcpy(
        destination_path,
        destination
    );

    strcat(
        destination_path,
        "\\"
    );

    strcat(
        destination_path,
        entries[selected].name
    );


    /*
     * Don't copy a directory into itself.
     */
    if (
        strcmp(
            source,
            destination_path
        ) == 0
    )
    {
        fm_message(
            "[ERROR]",
            "SAME LOCATION"
        );

        return;
    }


    if (
        entries[selected].type ==
        ITEM_DIRECTORY
    )
    {
        result =
            fm_copy_tree(
                source,
                destination_path
            );
    }
    else
    {
        result =
            fm_copy_file(
                source,
                destination_path
            );
    }


    if (result < 0)
    {
        fm_message(
            "[ERROR]",
            "COPY FAILED"
        );

        return;
    }


    if (move)
    {
        if (
            entries[selected].type ==
            ITEM_DIRECTORY
        )
        {
            result =
                fm_delete_tree(
                    source
                );
        }
        else
        {
            result =
                fm_delete_file(
                    source
                );
        }


        if (result < 0)
        {
            fm_message(
                "[ERROR]",
                "MOVE DELETE FAILED"
            );

            return;
        }


        fm_message(
            "[FILES]",
            "MOVE COMPLETE"
        );
    }
    else
    {
        fm_message(
            "[FILES]",
            "COPY COMPLETE"
        );
    }


    fm_load();
}


/* ============================================================
 * TEXT VIEWER
 * ============================================================
 */

static void fm_view(void)
{
    char path[PATH_SIZE];

    FONTCHARACTER fpath[PATH_SIZE];

    unsigned char buffer[241];

    char line[22];

    int handle;
    int size;
    int offset;
    int bytes;

    int p;
    int row;
    int i;

    unsigned int key;


    if (entry_count <= 0)
    {
        return;
    }


    if (
        entries[selected].type ==
        ITEM_DIRECTORY
    )
    {
        return;
    }


    fm_make_path(
        entries[selected].name,
        path
    );


    fm_to_font(
        path,
        fpath
    );


    handle =
        Bfile_OpenFile(
            fpath,
            _OPENMODE_READ
        );


    if (handle < 0)
    {
        fm_message(
            "[ERROR]",
            "OPEN FAILED"
        );

        return;
    }


    size =
        Bfile_GetFileSize(
            handle
        );


    if (size < 0)
    {
        Bfile_CloseFile(handle);
        return;
    }


    offset = 0;


    while (1)
    {
        fm_clear();


        fm_mini(
            1,
            1,
            entries[selected].name
        );


        fm_print(
            1,
            11,
            "TEXT VIEWER"
        );


        Bdisp_DrawLineVRAM(
            0,
            17,
            127,
            17
        );


        bytes =
            Bfile_ReadFile(
                handle,
                buffer,
                240,
                offset
            );


        if (bytes < 0)
        {
            bytes = 0;
        }


        p = 0;
        row = 0;


        while (
            p < bytes &&
            row < 5
        )
        {
            i = 0;


            while (
                p < bytes &&
                buffer[p] != '\n' &&
                buffer[p] != '\r' &&
                i < 21
            )
            {
                if (
                    buffer[p] < 32
                )
                {
                    line[i] = '.';
                }
                else
                {
                    line[i] =
                        (char)buffer[p];
                }

                i++;
                p++;
            }


            line[i] = 0;


            fm_print(
                1,
                20 + row * 8,
                line
            );


            while (
                p < bytes &&
                (
                    buffer[p] == '\r' ||
                    buffer[p] == '\n'
                )
            )
            {
                p++;
            }


            row++;
        }


        fm_mini(
            1,
            57,
            "UP/DN SCROLL EXIT"
        );


        fm_present();


        GetKey(&key);


        if (
            key == KEY_CTRL_EXIT
        )
        {
            break;
        }


        if (
            key == KEY_CTRL_DOWN
        )
        {
            offset += 100;

            if (
                offset >= size
            )
            {
                if (size > 0)
                {
                    offset = size - 1;
                }
                else
                {
                    offset = 0;
                }
            }
        }


        if (
            key == KEY_CTRL_UP
        )
        {
            offset -= 100;

            if (offset < 0)
            {
                offset = 0;
            }
        }
    }


    Bfile_CloseFile(
        handle
    );
}


/* ============================================================
 * OPERATIONS MENU
 * ============================================================
 */

static void fm_operations(void)
{
    unsigned int key;

    int choice;


    choice = 0;


    while (1)
    {
        fm_clear();


        fm_print(
            1,
            1,
            "[OPERATIONS]"
        );


        Bdisp_DrawLineVRAM(
            0,
            10,
            127,
            10
        );


        if (choice == 0)
            fm_print(1, 16, ">RENAME");
        else
            fm_print(1, 16, " RENAME");


        if (choice == 1)
            fm_print(1, 24, ">COPY");
        else
            fm_print(1, 24, " COPY");


        if (choice == 2)
            fm_print(1, 32, ">MOVE");
        else
            fm_print(1, 32, " MOVE");


        if (choice == 3)
            fm_print(1, 40, ">NEW FOLDER");
        else
            fm_print(1, 40, " NEW FOLDER");


        if (choice == 4)
            fm_print(1, 48, ">DELETE");
        else
            fm_print(1, 48, " DELETE");


        fm_mini(
            1,
            57,
            "EXE SELECT EXIT"
        );


        fm_present();


        GetKey(&key);


        if (
            key == KEY_CTRL_UP
        )
        {
            choice--;

            if (choice < 0)
            {
                choice = 4;
            }
        }


        else if (
            key == KEY_CTRL_DOWN
        )
        {
            choice++;

            if (choice > 4)
            {
                choice = 0;
            }
        }


        else if (
            key == KEY_CTRL_EXIT
        )
        {
            return;
        }


        else if (
            key == KEY_CTRL_EXE
        )
        {
            if (choice == 0)
            {
                fm_rename();
            }
            else if (choice == 1)
            {
                fm_copy_move(0);
            }
            else if (choice == 2)
            {
                fm_copy_move(1);
            }
            else if (choice == 3)
            {
                fm_new_directory();
            }
            else if (choice == 4)
            {
                fm_delete_selected();
            }
        }
    }
}


/* ============================================================
 * STORAGE INFO
 * ============================================================
 */

static void fm_storage_info(void)
{
    int free_bytes;
    int result;


    free_bytes = 0;


    result =
        Bfile_GetMediaFree(
            DEVICE_STORAGE,
            &free_bytes
        );


    fm_clear();


    fm_print(
        1,
        1,
        "[STORAGE]"
    );


    Bdisp_DrawLineVRAM(
        0,
        10,
        127,
        10
    );


    if (result < 0)
    {
        fm_print(
            1,
            22,
            "READ ERROR"
        );
    }
    else
    {
        fm_print(
            1,
            22,
            "FREE:"
        );


        fm_number(
            35,
            22,
            free_bytes
        );


        fm_print(
            1,
            35,
            "BYTES"
        );


        fm_print(
            1,
            47,
            "INTERNAL SMEM"
        );
    }


    fm_print(
        1,
        58,
        "EXE / EXIT"
    );


    fm_present();

    fm_wait();
}


/* ============================================================
 * STORAGE MEMORY BROWSER
 * ============================================================
 */

static void fm_smem(void)
{
    unsigned int key;


    fm_root();


    if (
        fm_load() < 0
    )
    {
        fm_message(
            "[ERROR]",
            "SMEM OPEN FAILED"
        );

        return;
    }


    while (1)
    {
        fm_draw_browser();


        GetKey(&key);


        /*
         * UP
         */
        if (
            key == KEY_CTRL_UP
        )
        {
            fm_up();
        }


        /*
         * DOWN
         */
        else if (
            key == KEY_CTRL_DOWN
        )
        {
            fm_down();
        }


        /*
         * EXE
         */
        else if (
            key == KEY_CTRL_EXE
        )
        {
            if (
                entry_count > 0
            )
            {
                if (
                    entries[selected].type ==
                    ITEM_DIRECTORY
                )
                {
                    if (
                        strcmp(
                            entries[selected].name,
                            ".."
                        ) == 0
                    )
                    {
                        fm_parent();
                        fm_load();
                    }
                    else if (
                        strcmp(
                            entries[selected].name,
                            "."
                        ) != 0
                    )
                    {
                        if (
                            strlen(current_path) +
                            strlen(
                                entries[selected].name
                            ) +
                            2 <
                            PATH_SIZE
                        )
                        {
                            strcat(
                                current_path,
                                "\\"
                            );

                            strcat(
                                current_path,
                                entries[selected].name
                            );

                            fm_load();
                        }
                    }
                }
                else
                {
                    fm_view();
                }
            }
        }


        /*
         * F1 INFO
         */
        else if (
            key == KEY_CTRL_F1
        )
        {
            fm_info();
        }


        /*
         * F2 OPERATIONS
         */
        else if (
            key == KEY_CTRL_F2
        )
        {
            fm_operations();
        }


        /*
         * MENU STORAGE INFO
         */
        else if (
            key == KEY_CTRL_MENU
        )
        {
            fm_storage_info();
        }


        /*
         * EXIT
         */
        else if (
            key == KEY_CTRL_EXIT
        )
        {
            if (
                strcmp(
                    current_path,
                    "\\\\fls0"
                ) == 0
            )
            {
                return;
            }


            fm_parent();
            fm_load();
        }
    }
}


/* ============================================================
 * ADD-IN ENTRY
 * ============================================================
 */

int AddIn_main(
    int isAppli,
    unsigned short OptionNum
)
{
    /*
     * Ignore these parameters.
     */
    isAppli = isAppli;
    OptionNum = OptionNum;


    fm_smem();


    Bdisp_AllClr_DDVRAM();


    return 1;
}


/* ============================================================
 * SYSTEM INITIALIZATION
 * ============================================================
 */

#pragma section _BR_Size

unsigned long BR_Size;

#pragma section


#pragma section _TOP

int InitializeSystem(
    int isAppli,
    unsigned short OptionNum
)
{
    return INIT_ADDIN_APPLICATION(
        isAppli,
        OptionNum
    );
}

#pragma section