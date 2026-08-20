#define SEH_TEXT "__try { ignored(); } __except (1) { ignored(); }"

/* __try { ignored(); } __except (1) { ignored(); } */
static const char* SehText = "__except (__try)";
static const char SehCharacter = '}';

int
SehLexicalFixture(
    void
)
{
    return SehText[0] + SehCharacter;
}
