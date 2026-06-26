/* stich.c */

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

typedef uint32_t bool_u32;
#define internal static

#ifdef STICH_SLOW
#define Assert(Expression) if(!(Expression)) { *(int*)0 = 0; }
#else
#define Assert(Expression)
#endif

#define PRINT_N_FLUSH(msg) do {fprintf(stderr,(msg));fflush(stderr);} while(0)

// TODO(eter): Add errno support.

// Whitespace chars credit: https://cpp0x.pl
// 0x09	tabulacja pozioma	'\t'
// 0x0A	przejście karetki do nowego wiersza	'\n'
// 0x0B	tabulacja pionowa	'\v'
// 0x0C	przesuwa karetkę na początek nowej strony	'\f'
// 0x0D	powrót karetki na początek wiersza	'\r'
// 0x20	spacja	' '
bool_u32 is_space(char c)
{
    return (((int)c >= 0x09 && (int)c <= 0x0D) || (int)c == 0x20);
}

bool_u32 is_digit(char c)
{ // '0' = 48 | '9' = 57
    return (c >= 48 && c <= 57);
}

typedef struct {
    uint8_t* data;
    uint64_t size;
} String8;

String8 os_load_entire_file(const char* filename)
{
    Assert(filename);
    String8 source_file = {0};

    int fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        return source_file;
    }
    struct stat st;
    if (fstat(fd, &st) < 0)
    {
        close(fd);
        return source_file;
    }

    source_file.size = st.st_size;
    if (source_file.size == 0)
    {
        close(fd);
        return source_file;
    }

    uint8_t* source = (uint8_t*)malloc(source_file.size + 1);
    if (!source)
    {
        close(fd);
        return source_file;
    }
    ssize_t bytes_read = read(fd, (void*)source, source_file.size);
    if (bytes_read != source_file.size)
    {
        free(source);
        return source_file;
    }

    source_file.data = source;
    source_file.data[source_file.size] = '\0';
    close(fd);

    return source_file;
}

typedef enum {
    TOKEN_IDENTIFIER = 0,
    TOKEN_NUMBER,
    TOKEN_LET,
    TOKEN_ASSIGN,
    TOKEN_ADD,
    TOKEN_SUB,
    TOKEN_MUL,
    TOKEN_DIV,
    TOKEN_PRINT,
    TOKEN_SEMICOLON,
    TOKEN_COUNT
} token_kind;

typedef struct {
    token_kind kind;
    const char* name;
    int len;
} Keyword;

typedef struct {
    token_kind kind;
    union {
        struct { uint8_t* text; uint64_t len; } indentifier;
        int64_t number;
    } value; 
} Token; 

Keyword keywords[] =
{
    {TOKEN_LET, "let", 3},
    {TOKEN_PRINT, "print", 5},
};
#define KEYWORD_COUNT (sizeof(keywords) / sizeof(keywords[0]))

internal const char* token_kind_to_string(token_kind kind)
{
    static const char* token_strings[] = 
    {
        "TOKEN_IDENTIFIER",
        "TOKEN_NUMBER",
        "TOKEN_LET",
        "TOKEN_ASSIGN",
        "TOKEN_ADD",
        "TOKEN_SUB",
        "TOKEN_MUL",
        "TOKEN_DIV",
        "TOKEN_PRINT",
        "TOKEN_SEMICOLON",
        "TOKEN_COUNT"
    };

    if (kind < 0 || kind >= TOKEN_COUNT) {
        return "UNKNOWN_TOKEN";
    }

    return token_strings[kind];
}

// We know that TOKEN_IDENTIFIER is not a 1 lenght operator
// so im using here as a false sentinel value.
// this macro goes with this nicely.
#define ONE_LENGHT_TOKEN(tk) ((tk) != TOKEN_IDENTIFIER)
token_kind is_one_lenght_operator(char c)
{
    switch (c)
    {
        case '+': return TOKEN_ADD;
        case '-': return TOKEN_SUB;
        case '*': return TOKEN_MUL;
        case '/': return TOKEN_DIV;
        case '=': return TOKEN_ASSIGN;
        case ';': return TOKEN_SEMICOLON;
        default:
            return TOKEN_IDENTIFIER;
    }
}

#define LEXER_DEFAULT_SCRATCH_CAPACITY (4096)
#define LEXER_DEFAULT_TOKEN_CAPACITY (1024)

typedef struct {
    uint8_t* cursor;
    uint8_t* end;

// NOTE(eter):
// The Scratch Buffer Friction: As we discussed earlier,
// copying characters byte-by-byte into a word_scratch_bytes array
// is unnecessary because the characters are already sitting 
// perfectly still in your String8 buffer. 
// A veteran compiler dev would just track the start 
// pointer and the length inside the file buffer 
// (a "string view" or "slice"), avoiding the extra memory writes entirely.
    uint8_t* word_scratch_bytes;
    uint32_t word_scratch_count;
    uint32_t word_scratch_capacity;

    Token* tokens;
    uint32_t tokens_count;
    uint32_t tokens_capacity;
} Lexer;

// This acts as a passthrough return type
typedef struct {
    Token* toks;
    uint32_t count;
} Tokens;

bool_u32 lexer_append_token(Lexer* lexer, Token token)
{
    if (lexer->tokens_count < lexer->tokens_capacity)
    {
        lexer->tokens[lexer->tokens_count++] = token;
        return (1);
    }
    return (0);
}

bool_u32 lexer_word_scratch_append(Lexer* lexer, uint8_t c)
{
    if (lexer->word_scratch_count < lexer->word_scratch_capacity)
    {
        lexer->word_scratch_bytes[lexer->word_scratch_count++] = c; 
        return (1);
    }
    return (0);
}

uint32_t lexer_scratch_to_number(Lexer* lexer)
{
    uint32_t result = 0;
    for (int i = 0; i < lexer->word_scratch_count; i++)
    {
        result = result * 10 + ((char)lexer->word_scratch_bytes[i] - '0');
    }
    return result;
}

Token lexer_get_token_from_scratch(Lexer* lexer)
{
    Token result = {0};

    // iterate over existing keywords and check for match.
    for (int i = 0; i < KEYWORD_COUNT; i++)
    {
        Keyword kw = keywords[i];
        if (lexer->word_scratch_count == kw.len)
        {
            int ret = memcmp((void*)kw.name,
                             (void*)lexer->word_scratch_bytes,
                                lexer->word_scratch_count);
            if (ret == 0)
            {
                result.kind = kw.kind;
                return result;
            }
        }
        
    }
    
    bool_u32 accumulated_word_is_a_number = (1);
    for (int i = 0; i < lexer->word_scratch_count; i++)
    {
        uint8_t c = *(lexer->word_scratch_bytes + i);
        if (!is_digit((char)c))
        {
            accumulated_word_is_a_number = (0);
            break;
        }
    }

    if (accumulated_word_is_a_number)
    {
        uint32_t number = lexer_scratch_to_number(lexer);
        result.kind = TOKEN_NUMBER;
        // TODO     int64_t = uint32_t
        result.value.number = number;
        return result;
    }

    result.kind = TOKEN_IDENTIFIER;
    // TODO add str* and len
    return result;
}

internal inline bool_u32 lexer_scratch_empty(Lexer* lexer)
{
    return !lexer->word_scratch_count;
}

bool_u32 lexer_init(Lexer* lexer)
{
    if (lexer)
    {
        uint8_t* word_scratch = (uint8_t*)malloc(LEXER_DEFAULT_SCRATCH_CAPACITY);
        Token* toks = (Token*)malloc(sizeof(Token)*LEXER_DEFAULT_TOKEN_CAPACITY);
        Assert(word_scratch);
        Assert(toks);

        lexer->cursor = NULL;
        lexer->end = NULL;

        lexer->tokens = toks;
        lexer->tokens_count = 0;
        lexer->tokens_capacity = LEXER_DEFAULT_TOKEN_CAPACITY;

        lexer->word_scratch_bytes = word_scratch;
        lexer->word_scratch_count = 0;
        lexer->word_scratch_capacity = LEXER_DEFAULT_SCRATCH_CAPACITY;

        return (1);
    }

    return (0);
}

Tokens lexer_run(Lexer* lexer, String8 source_file)
{
    Assert(lexer);
    Assert(source_file.data);

    lexer->tokens_count = 0;
    lexer->word_scratch_count = 0;

    lexer->cursor = source_file.data;
    lexer->end = lexer->cursor + source_file.size;

    while (lexer->cursor < lexer->end)
    {
        uint8_t c = *lexer->cursor;
        uint8_t c_next = *(lexer->cursor+1);

        token_kind tok_kind = is_one_lenght_operator((char)c);
        if (is_space((char)c) || ONE_LENGHT_TOKEN(tok_kind))
        {
            if (!lexer_scratch_empty(lexer))
            {
                printf("PRINT SCRATCH CONTENT: %.*s LEN: %d\n",
                     lexer->word_scratch_count, (char*)lexer->word_scratch_bytes, lexer->word_scratch_count);
                Token result = lexer_get_token_from_scratch(lexer);
                Assert(lexer_append_token(lexer, result));
                lexer->word_scratch_count = 0; 
            }

            if (ONE_LENGHT_TOKEN(tok_kind))
            {
                Token tok = {0};
                tok.kind = tok_kind;
                Assert(lexer_append_token(lexer, tok));
            }
        }
        else
        {
            Assert(lexer_word_scratch_append(lexer, c));
        }

        lexer->cursor++;
    }

    Tokens toks;
    toks.toks = lexer->tokens;
    toks.count = lexer->tokens_count;
    return toks;
}


void lexer_destroy(Lexer* lexer)
{
    if (lexer)
    {
        lexer->cursor = NULL;
        lexer->end = NULL;
        lexer->tokens_count = 0;
        lexer->tokens_capacity = 0;
        lexer->word_scratch_count = 0;
        lexer->word_scratch_capacity = 0;
        free(lexer->tokens);
        free(lexer->word_scratch_bytes);
        lexer->tokens = NULL;
        lexer->word_scratch_bytes = NULL;
    }
}

int main(int argc, char** argv)
{
    printf("[Hello, STICH]\n\n");

#ifndef DEBUG
    if (argc < 2)
    {
        printf("[STICH ERROR: No input file!]\n");
        return 1;
    }
#endif

#ifdef DEBUG /* This was done mostly to help with gdb argv passing crap XD */
    String8 source_file = os_load_entire_file("main.stich");
#else
    String8 source_file = os_load_entire_file(argv[1]);
#endif

    if (source_file.data == NULL)
    {
#ifdef DEBUG
        printf("[STICH ERROR: Could not read file 'main.stich']\n");
#else
        printf("[STICH ERROR: Could not read file '%s']\n", argv[1]);
#endif
        return 1;
    }

    Lexer lexer;
    Assert(lexer_init(&lexer));
    Tokens toks = lexer_run(&lexer, source_file);
    Assert(toks.toks);
    for (int i = 0; i < toks.count; i++)
    {
        printf("[%s]\n", token_kind_to_string(toks.toks[i].kind));
    }
    printf("[TOKENS GATHERED: %u]\n", toks.count);

    lexer_destroy(&lexer);
    return 0;
}

// let me leave this for a record XD 
// if (*(cursor+0) == 'l' && *(cursor+1) == 'e' && *(cursor+2) == 't' && is_space(*(cursor+3)))
// {
//     printf("let let");
//     cursor+=3;
//     continue;
// }

// Whitespace chars.
// 0x09	tabulacja pozioma	'\t'
// 0x0A	przejście karetki do nowego wiersza	'\n'
// 0x0B	tabulacja pionowa	'\v'
// 0x0C	przesuwa karetkę na początek nowej strony	'\f'
// 0x0D	powrót karetki na początek wiersza	'\r'
// 0x20	spacja	' '

// SOME RANDOM SHIT I'VE DONE PREVIOUSLY
//Capture the "Low" bits: Take rand() and mask it to 16 bits.
//Move them: Shift those bits 16 places to the left. (Now they are in the high zone).
//Capture more bits: Take a second rand() and mask it to 16 bits.
//Stitch: OR them together.
//
//Call 1: Take the bits and shift them left by 30 (fills bits 30 and 31).
// Call 2: Take the bits and shift them left by 15 (fills bits 15 through 29).
// Call 3: Take the bits and leave them at the bottom (fills bits 0 through 14).
//
// uint32_t get_random_bits_uint32_t(void)
// { // 0x00007fff masks first 15 bits;
//     uint32_t r = 0;
//     r |= ((uint32_t)(rand() & 0x00007fff) << 17);
//     r |= ((uint32_t)(rand() & 0x00007fff) << 2);
//     r |= ((uint32_t)(rand() & 0x00000003));
//     return r;
// }