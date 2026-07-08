/*
    mdcore -- the pure, Toolbox-free markdown engine at the heart of
    ArtfulType.

    Everything here operates on plain char buffers and the small model
    structs below. It includes no Mac headers, references no TextEdit
    record and no globals, so it compiles and runs on any host with a C
    compiler -- which is what lets it be unit-tested in milliseconds
    (see tests/). The Mac side (markdown.c) is a thin adapter: it locks
    the TextEdit handles, calls into mdcore, and maps the resulting spans
    onto real TextStyle runs. The mapping from a markdown "kind" to a
    concrete bold/Monaco/heading-size style lives only in that adapter.
*/
#ifndef MDCORE_H
#define MDCORE_H

/* Caps shared by the pure core and the Mac adapter. */
#define MD_MAX_LINKS 64
#define MD_MAX_SPANS 512

/* A styled span, expressed in the STRIPPED text's own coordinates
   (i.e. after the delimiter characters have been removed). kind is one
   of the MD_KIND_* letters below; level is 1..3 for a heading, else 0;
   linkID is a 1-based index into an MdLinkTable for a link, else 0. */
#define MD_KIND_BOLD    'B'
#define MD_KIND_ITALIC  'I'
#define MD_KIND_CODE    'C'
#define MD_KIND_LINK    'L'
#define MD_KIND_HEADING 'H'

typedef struct {
    long  start;
    long  end;
    short kind;
    short level;
    short linkID;
} MdSpan;

/* Link URLs, stored Pascal-string style (byte [0] is the length). Index 0
   is unused; valid link IDs run 1..count, matching the linkID in MdSpan. */
typedef struct {
    short         count;
    unsigned char url[MD_MAX_LINKS + 1][256];
} MdLinkTable;

/* How MdStrip treats a leading "# " at the start of a line. */
#define MD_HEADINGS_OFF   0  /* ignore '#'; leave "# " literal (paste path) */
#define MD_HEADINGS_SPAN  1  /* strip the whole heading, record an H span   */
                             /* over its body (the Writer-view path)        */
#define MD_HEADINGS_STRIP 2  /* strip only the "# " prefix and keep parsing */
                             /* the body inline, recording no span (the     */
                             /* "clear formatting" path)                    */

/* Options controlling MdStrip. */
typedef struct {
    int headingMode;       /* one of MD_HEADINGS_* above                   */
    int startsAtLineStart; /* is src[0] the first character of a line?     */
} MdStripOpts;

/*
    Strips markdown delimiters (**bold**, *italic*, `code`, [text](url),
    and leading "# " headings) from src[0..len), writing the surviving
    text to out and recording where each styled run landed.

    - out must have room for at least len bytes (stripping never grows the
      text). Returns the stripped length written to out.
    - spans receives up to spanCap styled runs; *spanCount gets the count.
      Runs past spanCap are still stripped, just not recorded (matching the
      old MAX_STYLE_OPS behaviour).
    - links receives the URLs found (reset to empty first). Pass NULL if the
      caller doesn't need them (e.g. "clear formatting"); link text is still
      stripped, spans just carry linkID 0.

    Pure: no allocation, no globals, no Toolbox.
*/
long MdStrip(const char *src, long len, const MdStripOpts *opts,
             char *out, long outCap,
             MdSpan *spans, short spanCap, short *spanCount,
             MdLinkTable *links);

#endif /* MDCORE_H */
