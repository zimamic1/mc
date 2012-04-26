#ifndef MC__EDIT_ASPELL_DIALOGS_H
#define MC__EDIT_ASPELL_DIALOGS_H

#include "lib/global.h"         /* include <glib.h> */
#include "lib/widget.h"         /* Widget */

#include "src/editor/edit.h"
#include "src/editor/spell.h"

/*** typedefs(not structures) and defined constants **********************************************/

#define B_SKIP_WORD (B_USER+3)
#define B_ADD_WORD (B_USER+4)

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

int editcmd_dialog_spell_suggest_show (WEdit *, const char *, char **, GArray *);
char *editcmd_dialog_lang_list_show (WEdit *, GArray *);

/*** inline functions ****************************************************************************/

#endif /* MC__EDIT_ASPELL_DIALOGS_H */
