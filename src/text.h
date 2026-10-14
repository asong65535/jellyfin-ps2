#ifndef TEXT_H
#define TEXT_H

/*
 * On-screen text is kept as Latin-1 bytes: one byte per letter, and the
 * font atlas has a glyph for each Latin-1 letter (French, Spanish, German,
 * Italian, Portuguese).
 *
 * Writes the Latin-1 spelling of one Unicode code point to out and returns
 * the bytes written (1 to 3). Letters outside Latin-1 that have a common
 * plain spelling get it - oe for the ligature, straight quotes for curly
 * ones, three dots for an ellipsis - and anything else becomes '?'.
 */
int text_put_cp(unsigned int cp, char *out);

/*
 * Converts UTF-8 to Latin-1, in place or into a buffer at least as long:
 * the result is never longer than the input. A byte that is not valid
 * UTF-8 is taken as Latin-1 already, so a file that was never UTF-8 still
 * reads. Returns the new length.
 */
int text_utf8_to_latin1(const char *in, int len, char *out);

#endif
