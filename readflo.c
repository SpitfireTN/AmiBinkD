#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "Config.h"
#include "sys.h"
#include "tools.h"
#include "readflo.h"

/*
 * Reads a line from a flo to dst[MAXPATHLEN], sets action
 * 1 -- ok
 * 0 -- EOF
 *
 * Improvements:
 * - Handles CRLF safely
 * - Protects against empty lines with whitespace
 * - Ensures dst is always NUL‑terminated
 * - Avoids strcpy() overflow risk
 */
int read_flo_line(char *dst, int *action, FILE *flo)
{
  char buf[MAXPATHLEN + 2];   /* +2 for safety */
  int len, i;

  for (;;)
  {
    if (!fgets(buf, sizeof(buf), flo))
      return 0; /* EOF */

    /* Strip trailing whitespace including CR/LF */
    len = strlen(buf);
    while (len > 0 && isspace((unsigned char)buf[len - 1]))
      buf[--len] = '\0';

    if (len == 0)
      continue; /* blank line */

    switch (buf[0])
    {
      case 0:
      case '~':
        continue; /* comment or empty */

      case '^':
        *action = 'd';
        strnzcpy(dst, buf + 1, MAXPATHLEN);
        break;

      case '#':
        *action = 't';
        strnzcpy(dst, buf + 1, MAXPATHLEN);
        break;

      default:
        *action = 0;
        strnzcpy(dst, buf, MAXPATHLEN);
        break;
    }
    break;
  }

  return 1;
}

/*
 * Translates a flo line using rf_rules.
 * Returns NULL if no rf_rules defined.
 *
 * Improvements:
 * - Uses local buffer safely
 * - Protects against NULL returns from ed()
 * - Ensures final string is always valid
 */
char *trans_flo_line(char *s, RF_RULE *rf_rules)
{
  RF_RULE *curr;
  char buf[MAXPATHLEN + 1];

  if (!rf_rules)
    return NULL;

  strnzcpy(buf, s, MAXPATHLEN);

  for (curr = rf_rules; curr; curr = curr->next)
  {
    char *w = ed(buf, curr->from, curr->to, NULL);

    if (w)
    {
      strnzcpy(buf, w, MAXPATHLEN);
      free(w);
    }
    else
    {
      /* ed() failed — keep original buffer */
    }
  }

  return xstrdup(buf);
}
