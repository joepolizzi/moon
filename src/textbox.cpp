/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * textbox.cpp: 
 *
 * Contact:
 *   Moonlight List (moonlight-list@lists.ximian.com)
 *
 * Copyright 2007 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 */

#include <config.h>

#include <cairo.h>

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "dependencyproperty.h"
#include "contentcontrol.h"
#include "timemanager.h"
#include "runtime.h"
#include "textbox.h"
#include "border.h"
#include "panel.h"
#include "utils.h"
#include "uri.h"
#include "factory.h"
#include "contentpresenter.h"

#include "window.h"

#include "geometry.h"
#include "managedtypeinfo.h"
#include "contentpresenter.h"

namespace Moonlight {

//
// TextBuffer
//

#define UNICODE_LEN(size) (sizeof (gunichar) * (size))
#define UNICODE_OFFSET(buf,offset) (((char *) buf) + UNICODE_LEN (offset))

class TextBuffer {
	int allocated;
	
	bool Resize (int needed)
	{
		int new_size = allocated;
		bool resize = false;
		void *buf;
		
		if (allocated >= needed + 128) {
			while (new_size >= needed + 128)
				new_size -= 128;
			resize = true;
		} else if (allocated < needed) {
			while (new_size < needed)
				new_size += 128;
			resize = true;
		}
		
		if (resize) {
			if (!(buf = g_try_realloc (text, UNICODE_LEN (new_size)))) {
				// if new_size is < allocated, then we can pretend like we succeeded
				return new_size < allocated;
			}
			
			text = (gunichar *) buf;
			allocated = new_size;
		}
		
		return true;
	}
	
 public:
	gunichar *text;
	int len;
	
	TextBuffer (const gunichar *text, int len)
	{
		this->allocated = 0;
		this->text = NULL;
		this->len = 0;
		
		Append (text, len);
	}
	
	TextBuffer ()
	{
		text = NULL;
		Reset ();
	}
	
	void Reset ()
	{
		text = (gunichar *) g_realloc (text, UNICODE_LEN (128));
		allocated = 128;
		text[0] = '\0';
		len = 0;
	}
	
	void Print ()
	{
		printf ("TextBuffer::text = \"");
		
		for (int i = 0; i < len; i++) {
			switch (text[i]) {
			case '\r':
				fputs ("\\r", stdout);
				break;
			case '\n':
				fputs ("\\n", stdout);
				break;
			case '\0':
				fputs ("\\0", stdout);
				break;
			case '\\':
				fputc ('\\', stdout);
				// fall thru
			default:
				fputc ((char) text[i], stdout);
				break;
			}
		}
		
		printf ("\";\n");
	}
	
	void Append (gunichar c)
	{
		if (!Resize (len + 2))
			return;
		
		text[len++] = c;
		text[len] = 0;
	}
	
	void Append (const gunichar *str, int count)
	{
		if (!Resize (len + count + 1))
			return;
		
		memcpy (UNICODE_OFFSET (text, len), str, UNICODE_LEN (count));
		len += count;
		text[len] = 0;
	}
	
	void Cut (int start, int length)
	{
		char *dest, *src;
		int beyond;
		
		if (length == 0 || start >= len)
			return;
		
		if (start + length > len)
			length = len - start;
		
		src = UNICODE_OFFSET (text, start + length);
		dest = UNICODE_OFFSET (text, start);
		beyond = len - (start + length);
		
		memmove (dest, src, UNICODE_LEN (beyond + 1));
		len -= length;
	}
	
	void Insert (int index, gunichar c)
	{
		if (!Resize (len + 2))
			return;
		
		if (index < len) {
			// shift all chars beyond position @index down by 1 char
			memmove (UNICODE_OFFSET (text, index + 1), UNICODE_OFFSET (text, index), UNICODE_LEN ((len - index) + 1));
			text[index] = c;
			len++;
		} else {
			text[len++] = c;
			text[len] = 0;
		}
	}
	
	void Insert (int index, const gunichar *str, int count)
	{
		if (!Resize (len + count + 1))
			return;
		
		if (index < len) {
			// shift all chars beyond position @index down by @count chars
			memmove (UNICODE_OFFSET (text, index + count), UNICODE_OFFSET (text, index), UNICODE_LEN ((len - index) + 1));
			
			// insert @count chars of @str into our buffer at position @index
			memcpy (UNICODE_OFFSET (text, index), str, UNICODE_LEN (count));
			len += count;
		} else {
			// simply append @count chars of @str onto the end of our buffer
			memcpy (UNICODE_OFFSET (text, len), str, UNICODE_LEN (count));
			len += count;
			text[len] = 0;
		}
	}
	
	void Prepend (gunichar c)
	{
		if (!Resize (len + 2))
			return;
		
		// shift the entire buffer down by 1 char
		memmove (UNICODE_OFFSET (text, 1), text, UNICODE_LEN (len + 1));
		text[0] = c;
		len++;
	}
	
	void Prepend (const gunichar *str, int count)
	{
		if (!Resize (len + count + 1))
			return;
		
		// shift the endtire buffer down by @count chars
		memmove (UNICODE_OFFSET (text, count), text, UNICODE_LEN (len + 1));
		
		// copy @count chars of @str into the beginning of our buffer
		memcpy (text, str, UNICODE_LEN (count));
		len += count;
	}
	
	void Replace (int start, int length, const gunichar *str, int count)
	{
		char *dest, *src;
		int beyond;
		
		if (start > len)
			return;
		
		if (start + length > len)
			length = len - start;
		
		// Check for the easy cases first...
		if (length == 0) {
			Insert (start, str, count);
			return;
		} else if (count == 0) {
			Cut (start, length);
			return;
		} else if (count == length) {
			memcpy (UNICODE_OFFSET (text, start), str, UNICODE_LEN (count));
			return;
		}
		
		if (count > length && !Resize (len + (count - length) + 1))
			return;
		
		// calculate the number of chars beyond @start that won't be cut
		beyond = len - (start + length);
		
		// shift all chars beyond position (@start + length) into position...
		dest = UNICODE_OFFSET (text, start + count);
		src = UNICODE_OFFSET (text, start + length);
		memmove (dest, src, UNICODE_LEN (beyond + 1));
		
		// copy @count chars of @str into our buffer at position @start
		memcpy (UNICODE_OFFSET (text, start), str, UNICODE_LEN (count));
		
		len = (len - length) + count;
	}
	
	gunichar *Substring (int start, int length = -1)
	{
		gunichar *substr;
		size_t n_bytes;
		
		if (start < 0 || start > len || length == 0)
			return NULL;
		
		if (length < 0)
			length = len - start;
		
		n_bytes = sizeof (gunichar) * (length + 1);
		substr = (gunichar *) g_malloc (n_bytes);
		n_bytes -= sizeof (gunichar);
		
		memcpy (substr, text + start, n_bytes);
		substr[length] = 0;
		
		return substr;
	}
};


//
// TextBoxUndoActions
//

enum TextBoxUndoActionType {
	TextBoxUndoActionTypeInsert,
	TextBoxUndoActionTypeDelete,
	TextBoxUndoActionTypeReplace,
};

class TextBoxUndoAction : public List::Node {
 public:
	TextBoxUndoActionType type;
	int selection_anchor;
	int selection_cursor;
	int length;
	int start;
};

class TextBoxUndoActionInsert : public TextBoxUndoAction {
 public:
	TextBuffer *buffer;
	bool growable;
	
	TextBoxUndoActionInsert (int selection_anchor, int selection_cursor, int start, gunichar c);
	TextBoxUndoActionInsert (int selection_anchor, int selection_cursor, int start, const gunichar *inserted, int length, bool atomic = false);
	virtual ~TextBoxUndoActionInsert ();
	
	bool Insert (int start, const gunichar *text, int len);
	bool Insert (int start, gunichar c);
};

class TextBoxUndoActionDelete : public TextBoxUndoAction {
 public:
	gunichar *text;
	
	TextBoxUndoActionDelete (int selection_anchor, int selection_cursor, TextBuffer *buffer, int start, int length);
	virtual ~TextBoxUndoActionDelete ();
};

class TextBoxUndoActionReplace : public TextBoxUndoAction {
 public:
	gunichar *inserted;
	gunichar *deleted;
	int inlen;
	
	TextBoxUndoActionReplace (int selection_anchor, int selection_cursor, TextBuffer *buffer, int start, int length, const gunichar *inserted, int inlen);
	TextBoxUndoActionReplace (int selection_anchor, int selection_cursor, TextBuffer *buffer, int start, int length, gunichar c);
	virtual ~TextBoxUndoActionReplace ();
};

class TextBoxUndoStack {
	int max_count;
	List *list;
	
 public:
	TextBoxUndoStack (int max_count);
	~TextBoxUndoStack ();
	
	bool IsEmpty ();
	void Clear ();
	
	void Push (TextBoxUndoAction *action);
	TextBoxUndoAction *Peek ();
	TextBoxUndoAction *Pop ();
};

TextBoxUndoActionInsert::TextBoxUndoActionInsert (int selection_anchor, int selection_cursor, int start, gunichar c)
{
	this->type = TextBoxUndoActionTypeInsert;
	this->selection_anchor = selection_anchor;
	this->selection_cursor = selection_cursor;
	this->start = start;
	this->length = 1;
	
	this->buffer = new TextBuffer ();
	this->buffer->Append (c);
	this->growable = true;
}

TextBoxUndoActionInsert::TextBoxUndoActionInsert (int selection_anchor, int selection_cursor, int start, const gunichar *inserted, int length, bool atomic)
{
	this->type = TextBoxUndoActionTypeInsert;
	this->selection_anchor = selection_anchor;
	this->selection_cursor = selection_cursor;
	this->length = length;
	this->start = start;
	
	this->buffer = new TextBuffer (inserted, length);
	this->growable = !atomic;
}

TextBoxUndoActionInsert::~TextBoxUndoActionInsert ()
{
	delete buffer;
}

bool
TextBoxUndoActionInsert::Insert (int start, const gunichar *text, int len)
{
	if (!growable || start != (this->start + length))
		return false;
	
	buffer->Append (text, len);
	length += len;
	
	return true;
}

bool
TextBoxUndoActionInsert::Insert (int start, gunichar c)
{
	if (!growable || start != (this->start + length))
		return false;
	
	buffer->Append (c);
	length++;
	
	return true;
}

TextBoxUndoActionDelete::TextBoxUndoActionDelete (int selection_anchor, int selection_cursor, TextBuffer *buffer, int start, int length)
{
	this->type = TextBoxUndoActionTypeDelete;
	this->selection_anchor = selection_anchor;
	this->selection_cursor = selection_cursor;
	this->length = length;
	this->start = start;
	
	this->text = buffer->Substring (start, length);
}

TextBoxUndoActionDelete::~TextBoxUndoActionDelete ()
{
	g_free (text);
}

TextBoxUndoActionReplace::TextBoxUndoActionReplace (int selection_anchor, int selection_cursor, TextBuffer *buffer, int start, int length, const gunichar *inserted, int inlen)
{
	this->type = TextBoxUndoActionTypeReplace;
	this->selection_anchor = selection_anchor;
	this->selection_cursor = selection_cursor;
	this->length = length;
	this->start = start;
	
	this->deleted = buffer->Substring (start, length);
	this->inserted = (gunichar *) g_malloc (UNICODE_LEN (inlen + 1));
	memcpy (this->inserted, inserted, UNICODE_LEN (inlen + 1));
	this->inlen = inlen;
}

TextBoxUndoActionReplace::TextBoxUndoActionReplace (int selection_anchor, int selection_cursor, TextBuffer *buffer, int start, int length, gunichar c)
{
	this->type = TextBoxUndoActionTypeReplace;
	this->selection_anchor = selection_anchor;
	this->selection_cursor = selection_cursor;
	this->length = length;
	this->start = start;
	
	this->deleted = buffer->Substring (start, length);
	this->inserted = g_new (gunichar, 2);
	memcpy (inserted, &c, sizeof (gunichar));
	inserted[1] = 0;
	this->inlen = 1;
}

TextBoxUndoActionReplace::~TextBoxUndoActionReplace ()
{
	g_free (inserted);
	g_free (deleted);
}


TextBoxUndoStack::TextBoxUndoStack (int max_count)
{
	this->max_count = max_count;
	this->list = new List ();
}

TextBoxUndoStack::~TextBoxUndoStack ()
{
	delete list;
}

bool
TextBoxUndoStack::IsEmpty ()
{
	return list->IsEmpty ();
}

void
TextBoxUndoStack::Clear ()
{
	list->Clear (true);
}

void
TextBoxUndoStack::Push (TextBoxUndoAction *action)
{
	if (list->Length () == max_count) {
		List::Node *node = list->Last ();
		list->Unlink (node);
		delete node;
	}
	
	list->Prepend (action);
}

TextBoxUndoAction *
TextBoxUndoStack::Pop ()
{
	List::Node *node = list->First ();
	
	if (node)
		list->Unlink (node);
	
	return (TextBoxUndoAction *) node;
}

TextBoxUndoAction *
TextBoxUndoStack::Peek ()
{
	return (TextBoxUndoAction *) list->First ();
}


//
// TextBoxBase
//

// emit state, also doubles as available event mask
#define NOTHING_CHANGED         (0)
#define SELECTION_CHANGED       (1 << 0)
#define TEXT_CHANGED            (1 << 1)

#define COMMAND_MASK Runtime::GetWindowingSystem()->GetCommandModifier()
#define SHIFT_MASK   MoonModifier_Shift
#define ALT_MASK     MoonModifier_Alt

#define IsEOL(c) ((c) == '\r' || (c) == '\n')

static MoonWindow *
GetNormalWindow (TextBoxBase *textbox)
{
	if (!textbox->IsAttached ())
		return NULL;
	
	return textbox->GetDeployment ()->GetSurface ()->GetNormalWindow ();
}

static MoonClipboard *
GetClipboard (TextBoxBase *textbox, MoonClipboardType clipboardType)
{
	MoonWindow *window = GetNormalWindow(textbox);

	if (!window)
		return NULL;

	return window->GetClipboard (clipboardType);
}

void
TextBoxBase::Initialize (Type::Kind type)
{
	SetObjectType (type);

	ManagedTypeInfo type_info (GetObjectType ());
	SetDefaultStyleKey (&type_info);
	
	AddHandler (UIElement::MouseLeftButtonMultiClickEvent, TextBoxBase::mouse_left_button_multi_click, this);
	
	font = new TextFontDescription ();
	font->SetFamily (GetFontFamily ()->source);
	font->SetStretch (GetFontStretch ()->stretch);
	font->SetWeight (GetFontWeight ()->weight);
	font->SetStyle (GetFontStyle ()->style);
	font->SetSize (GetFontSize ());
	
	downloaders = g_ptr_array_new ();
	font_resource = NULL;
	
	contentElement = NULL;
	
	MoonWindowingSystem *ws = Runtime::GetWindowingSystem ();
	im_ctx = ws->CreateIMContext();
	im_ctx->SetUsePreedit (false);
	
	im_ctx->SetRetrieveSurroundingCallback ((MoonCallback)TextBoxBase::retrieve_surrounding, this);
	im_ctx->SetDeleteSurroundingCallback ((MoonCallback)TextBoxBase::delete_surrounding, this);
	im_ctx->SetCommitCallback ((MoonCallback)TextBoxBase::commit, this);
	
	undo = new TextBoxUndoStack (10);
	redo = new TextBoxUndoStack (10);
	buffer = new TextBuffer ();
	max_length = 0;
	
	emit = NOTHING_CHANGED;
	events_mask = 0;
	
	selection_anchor = 0;
	selection_cursor = 0;
	cursor_offset = 0.0;
	batch = 0;
	
	accepts_return = false;
	need_im_reset = false;
	is_read_only = false;
	have_offset = false;
	multiline = false;
	selecting = false;
	setvalue = true;
	captured = false;
	focused = false;
	secret = false;
	view = NULL;
}

TextBoxBase::~TextBoxBase ()
{
	ResetIMContext ();
	delete im_ctx;
	
	CleanupDownloaders ();
	g_ptr_array_free (downloaders, true);
	delete font_resource;
	
	delete buffer;
	delete undo;
	delete redo;
	delete font;
}

void
TextBoxBase::OnIsAttachedChanged (bool value)
{
	Control::OnIsAttachedChanged (value);

	Surface *surface = GetDeployment ()->GetSurface ();

	if (value) {
		surface->AddHandler (Surface::WindowAvailableEvent, TextBoxBase::AttachIMClientWindowCallback, this);
		surface->AddHandler (Surface::WindowUnavailableEvent, TextBoxBase::DetachIMClientWindowCallback, this);
	}
	else {
		if (surface) {
			surface->RemoveHandler (Surface::WindowAvailableEvent, TextBoxBase::AttachIMClientWindowCallback, this);
			surface->RemoveHandler (Surface::WindowUnavailableEvent, TextBoxBase::DetachIMClientWindowCallback, this);

			DetachIMClientWindowHandler (NULL, NULL);
		}
	}
}

void
TextBoxBase::AttachIMClientWindowHandler (EventObject *sender, EventArgs *calldata)
{
	im_ctx->SetClientWindow (GetDeployment ()->GetSurface ()->GetWindow());
}

void
TextBoxBase::DetachIMClientWindowHandler (EventObject *sender, EventArgs *calldata)
{
	im_ctx->SetClientWindow (NULL);
}

void
TextBoxBase::CleanupDownloaders ()
{
	Downloader *downloader;
	guint i;
	
	for (i = 0; i < downloaders->len; i++) {
		downloader = (Downloader *) downloaders->pdata[i];
		downloader->RemoveHandler (Downloader::CompletedEvent, downloader_complete, this);
		downloader->Abort ();
		downloader->unref ();
	}
	
	g_ptr_array_set_size (downloaders, 0);
}

double
TextBoxBase::GetCursorOffset ()
{
	if (!have_offset && view) {
		cursor_offset = view->GetCursor ().x;
		have_offset = true;
	}
	
	return cursor_offset;
}

int
TextBoxBase::CursorDown (int cursor, bool page)
{
	double y = view->GetCursor ().y;
	double x = GetCursorOffset ();
	TextLayoutLine *line;
	TextLayoutRun *run;
	int index, cur, n;
	guint i;
	
	if (!(line = view->GetLineFromY (y, &index)))
		return cursor;
	
	if (page) {
		// calculate the number of lines to skip over
		n = GetActualHeight () / line->height;
	} else {
		n = 1;
	}
	
	if (index + n >= view->GetLineCount ()) {
		// go to the end of the last line
		line = view->GetLineFromIndex (view->GetLineCount () - 1);
		
		for (cur = line->offset, i = 0; i < line->runs->len; i++) {
			run = (TextLayoutRun *) line->runs->pdata[i];
			cur += run->count;
		}
		
		have_offset = false;
		
		return cur;
	}
	
	line = view->GetLineFromIndex (index + n);
	
	return line->GetCursorFromX (Point (), x);
}

int
TextBoxBase::CursorUp (int cursor, bool page)
{
	double y = view->GetCursor ().y;
	double x = GetCursorOffset ();
	TextLayoutLine *line;
	int index, n;
	
	if (!(line = view->GetLineFromY (y, &index)))
		return cursor;
	
	if (page) {
		// calculate the number of lines to skip over
		n = GetActualHeight () / line->height;
	} else {
		n = 1;
	}
	
	if (index < n) {
		// go to the beginning of the first line
		have_offset = false;
		return 0;
	}
	
	line = view->GetLineFromIndex (index - n);
	
	return line->GetCursorFromX (Point (), x);
}

#ifdef EMULATE_GTK
enum CharClass {
	CharClassUnknown,
	CharClassWhitespace,
	CharClassAlphaNumeric
};

static inline CharClass
char_class (gunichar c)
{
	if (g_unichar_isspace (c))
		return CharClassWhitespace;
	
	if (g_unichar_isalnum (c))
		return CharClassAlphaNumeric;
	
	return CharClassUnknown;
}
#else
static bool
is_start_of_word (TextBuffer *buffer, int index)
{
	// A 'word' starts with an AlphaNumeric or some punctuation symbols immediately preceeded by lwsp
	if (index > 0 && !g_unichar_isspace (buffer->text[index - 1]))
		return false;
	
	switch (g_unichar_type (buffer->text[index])) {
	case G_UNICODE_LOWERCASE_LETTER:
	case G_UNICODE_TITLECASE_LETTER:
	case G_UNICODE_UPPERCASE_LETTER:
	case G_UNICODE_DECIMAL_NUMBER:
	case G_UNICODE_LETTER_NUMBER:
	case G_UNICODE_OTHER_NUMBER:
	case G_UNICODE_DASH_PUNCTUATION:
	case G_UNICODE_INITIAL_PUNCTUATION:
	case G_UNICODE_OPEN_PUNCTUATION:
	case G_UNICODE_CURRENCY_SYMBOL:
	case G_UNICODE_MATH_SYMBOL:
		return true;
	case G_UNICODE_OTHER_PUNCTUATION:
		// words cannot start with '.', but they can start with '&' or '*' (for example)
		return g_unichar_break_type (buffer->text[index]) == G_UNICODE_BREAK_ALPHABETIC;
	default:
		return false;
	}
}
#endif

int
TextBoxBase::CursorNextWord (int cursor)
{
	int i, lf, cr;
	
	// find the end of the current line
	cr = CursorLineEnd (cursor);
	if (buffer->text[cr] == '\r' && buffer->text[cr + 1] == '\n')
		lf = cr + 1;
	else
		lf = cr;
	
	// if the cursor is at the end of the line, return the starting offset of the next line
	if (cursor == cr || cursor == lf) {
		if (lf < buffer->len)
			return lf + 1;
		
		return cursor;
	}
	
#ifdef EMULATE_GTK
	CharClass cc = char_class (buffer->text[cursor]);
	i = cursor;
	
	// skip over the word, punctuation, or run of whitespace
	while (i < cr && char_class (buffer->text[i]) == cc)
		i++;
	
	// skip any whitespace after the word/punct
	while (i < cr && g_unichar_isspace (buffer->text[i]))
		i++;
#else
	i = cursor;
	
	// skip to the end of the current word
	while (i < cr && !g_unichar_isspace (buffer->text[i]))
		i++;
	
	// skip any whitespace after the word
	while (i < cr && g_unichar_isspace (buffer->text[i]))
		i++;
	
	// find the start of the next word
	while (i < cr && !is_start_of_word (buffer, i))
		i++;
#endif
	
	return i;
}

int
TextBoxBase::CursorPrevWord (int cursor)
{
	int begin, i, cr, lf;
	
	// find the beginning of the current line
	lf = CursorLineBegin (cursor) - 1;
	
	if (lf > 0 && buffer->text[lf] == '\n' && buffer->text[lf - 1] == '\r')
		cr = lf - 1;
	else
		cr = lf;
	
	// if the cursor is at the beginning of the line, return the end of the prev line
	if (cursor - 1 == lf) {
		if (cr > 0)
			return cr;
		
		return 0;
	}
	
#ifdef EMULATE_GTK
	CharClass cc = char_class (buffer->text[cursor - 1]);
	begin = lf + 1;
	i = cursor;
	
	// skip over the word, punctuation, or run of whitespace
	while (i > begin && char_class (buffer->text[i - 1]) == cc)
		i--;
	
	// if the cursor was at whitespace, skip back a word too
	if (cc == CharClassWhitespace && i > begin) {
		cc = char_class (buffer->text[i - 1]);
		while (i > begin && char_class (buffer->text[i - 1]) == cc)
			i--;
	}
#else
	begin = lf + 1;
	i = cursor;
	
	if (cursor < buffer->len) {
		// skip to the beginning of this word
		while (i > begin && !g_unichar_isspace (buffer->text[i - 1]))
			i--;
		
		if (i < cursor && is_start_of_word (buffer, i))
			return i;
	}
	
	// skip to the start of the lwsp
	while (i > begin && g_unichar_isspace (buffer->text[i - 1]))
		i--;
	
	if (i > begin)
		i--;
	
	// skip to the beginning of the word
	while (i > begin && !is_start_of_word (buffer, i))
		i--;
#endif
	
	return i;
}

int
TextBoxBase::CursorLineBegin (int cursor)
{
	int cur = cursor;
	
	// find the beginning of the line
	while (cur > 0 && !IsEOL (buffer->text[cur - 1]))
		cur--;
	
	return cur;
}

int
TextBoxBase::CursorLineEnd (int cursor, bool include)
{
	int cur = cursor;
	
	// find the end of the line
	while (cur < buffer->len && !IsEOL (buffer->text[cur]))
		cur++;
	
	if (include && cur < buffer->len) {
		if (buffer->text[cur] == '\r' && buffer->text[cur + 1] == '\n')
			cur += 2;
		else
			cur++;
	}
	
	return cur;
}

bool
TextBoxBase::KeyPressBackSpace (MoonModifier modifiers)
{
	int anchor = selection_anchor;
	int cursor = selection_cursor;
	TextBoxUndoAction *action;
	int start = 0, length = 0;
	bool handled = false;
	
	if ((modifiers & (ALT_MASK | SHIFT_MASK)) != 0)
		return false;
	
	if (cursor != anchor) {
		// BackSpace w/ active selection: delete the selected text
		length = abs (cursor - anchor);
		start = MIN (anchor, cursor);
	} else if ((modifiers & COMMAND_MASK) != 0) {
		// Ctrl+BackSpace: delete the word ending at the cursor
		start = CursorPrevWord (cursor);
		length = cursor - start;
	} else if (cursor > 0) {
		// BackSpace: delete the char before the cursor position
		if (cursor >= 2 && buffer->text[cursor - 1] == '\n' && buffer->text[cursor - 2] == '\r') {
			start = cursor - 2;
			length = 2;
		} else {
			start = cursor - 1;
			length = 1;
		}
	}
	
	if (length > 0) {
		action = new TextBoxUndoActionDelete (selection_anchor, selection_cursor, buffer, start, length);
		undo->Push (action);
		redo->Clear ();
		
		buffer->Cut (start, length);
		emit |= TEXT_CHANGED;
		anchor = start;
		cursor = start;
		handled = true;
	}
	
	// check to see if selection has changed
	if (selection_anchor != anchor || selection_cursor != cursor) {
		SetSelectionStart (MIN (anchor, cursor));
		SetSelectionLength (abs (cursor - anchor));
		selection_anchor = anchor;
		selection_cursor = cursor;
		emit |= SELECTION_CHANGED;
		handled = true;
	}
	
	return handled;
}

bool
TextBoxBase::KeyPressDelete (MoonModifier modifiers)
{
	int anchor = selection_anchor;
	int cursor = selection_cursor;
	TextBoxUndoAction *action;
	int start = 0, length = 0;
	bool handled = false;
	
	if ((modifiers & (ALT_MASK | SHIFT_MASK)) != 0)
		return false;
	
	if (cursor != anchor) {
		// Delete w/ active selection: delete the selected text
		length = abs (cursor - anchor);
		start = MIN (anchor, cursor);
	} else if ((modifiers & COMMAND_MASK) != 0) {
		// Ctrl+Delete: delete the word starting at the cursor
		length = CursorNextWord (cursor) - cursor;
		start = cursor;
	} else if (cursor < buffer->len) {
		// Delete: delete the char after the cursor position
		if (buffer->text[cursor] == '\r' && buffer->text[cursor + 1] == '\n')
			length = 2;
		else
			length = 1;
		
		start = cursor;
	}
	
	if (length > 0) {
		action = new TextBoxUndoActionDelete (selection_anchor, selection_cursor, buffer, start, length);
		undo->Push (action);
		redo->Clear ();
		
		buffer->Cut (start, length);
		emit |= TEXT_CHANGED;
		handled = true;
	}
	
	// check to see if selection has changed
	if (selection_anchor != anchor || selection_cursor != cursor) {
		SetSelectionStart (MIN (anchor, cursor));
		SetSelectionLength (abs (cursor - anchor));
		selection_anchor = anchor;
		selection_cursor = cursor;
		emit |= SELECTION_CHANGED;
		handled = true;
	}
	
	return handled;
}

bool
TextBoxBase::KeyPressPageDown (MoonModifier modifiers)
{
	int anchor = selection_anchor;
	int cursor = selection_cursor;
	bool have;
	
	if ((modifiers & (COMMAND_MASK | ALT_MASK)) != 0)
		return false;
	
	// move the cursor down one page from its current position
	cursor = CursorDown (cursor, true);
	have = have_offset;
	
	if ((modifiers & SHIFT_MASK) == 0) {
		// clobber the selection
		anchor = cursor;
	}
	
	// check to see if selection has changed
	if (selection_anchor != anchor || selection_cursor != cursor) {
		SetSelectionStart (MIN (anchor, cursor));
		SetSelectionLength (abs (cursor - anchor));
		selection_anchor = anchor;
		selection_cursor = cursor;
		emit |= SELECTION_CHANGED;
		have_offset = have;
	}
	
	return true;
}

bool
TextBoxBase::KeyPressPageUp (MoonModifier modifiers)
{
	int anchor = selection_anchor;
	int cursor = selection_cursor;
	bool have;
	
	if ((modifiers & (COMMAND_MASK | ALT_MASK)) != 0)
		return false;
	
	// move the cursor up one page from its current position
	cursor = CursorUp (cursor, true);
	have = have_offset;
	
	if ((modifiers & SHIFT_MASK) == 0) {
		// clobber the selection
		anchor = cursor;
	}
	
	// check to see if selection has changed
	if (selection_anchor != anchor || selection_cursor != cursor) {
		SetSelectionStart (MIN (anchor, cursor));
		SetSelectionLength (abs (cursor - anchor));
		selection_anchor = anchor;
		selection_cursor = cursor;
		emit |= SELECTION_CHANGED;
		have_offset = have;
	}
	
	return true;
}

bool
TextBoxBase::KeyPressDown (MoonModifier modifiers)
{
	int anchor = selection_anchor;
	int cursor = selection_cursor;
	bool handled = false;
	bool have;
	
	if ((modifiers & (COMMAND_MASK | ALT_MASK)) != 0)
		return false;
	
	// move the cursor down by one line from its current position
	cursor = CursorDown (cursor, false);
	have = have_offset;
	
	if ((modifiers & SHIFT_MASK) == 0) {
		// clobber the selection
		anchor = cursor;
	}
	
	// check to see if selection has changed
	if (selection_anchor != anchor || selection_cursor != cursor) {
		SetSelectionStart (MIN (anchor, cursor));
		SetSelectionLength (abs (cursor - anchor));
		selection_anchor = anchor;
		selection_cursor = cursor;
		emit |= SELECTION_CHANGED;
		have_offset = have;
		handled = true;
	}
	
	return handled;
}

bool
TextBoxBase::KeyPressUp (MoonModifier modifiers)
{
	int anchor = selection_anchor;
	int cursor = selection_cursor;
	bool handled = false;
	bool have;
	
	if ((modifiers & (COMMAND_MASK | ALT_MASK)) != 0)
		return false;
	
	// move the cursor up by one line from its current position
	cursor = CursorUp (cursor, false);
	have = have_offset;
	
	if ((modifiers & SHIFT_MASK) == 0) {
		// clobber the selection
		anchor = cursor;
	}
	
	// check to see if selection has changed
	if (selection_anchor != anchor || selection_cursor != cursor) {
		SetSelectionStart (MIN (anchor, cursor));
		SetSelectionLength (abs (cursor - anchor));
		selection_anchor = anchor;
		selection_cursor = cursor;
		emit |= SELECTION_CHANGED;
		have_offset = have;
		handled = true;
	}
	
	return handled;
}

bool
TextBoxBase::KeyPressHome (MoonModifier modifiers)
{
	int anchor = selection_anchor;
	int cursor = selection_cursor;
	bool handled = false;
	
	if ((modifiers & ALT_MASK) != 0)
		return false;
	
	if ((modifiers & COMMAND_MASK) != 0) {
		// move the cursor to the beginning of the buffer
		cursor = 0;
	} else {
		// move the cursor to the beginning of the line
		cursor = CursorLineBegin (cursor);
	}
	
	if ((modifiers & SHIFT_MASK) == 0) {
		// clobber the selection
		anchor = cursor;
	}
	
	// check to see if selection has changed
	if (selection_anchor != anchor || selection_cursor != cursor) {
		SetSelectionStart (MIN (anchor, cursor));
		SetSelectionLength (abs (cursor - anchor));
		selection_anchor = anchor;
		selection_cursor = cursor;
		emit |= SELECTION_CHANGED;
		have_offset = false;
		handled = true;
	}
	
	return handled;
}

bool
TextBoxBase::KeyPressEnd (MoonModifier modifiers)
{
	int anchor = selection_anchor;
	int cursor = selection_cursor;
	bool handled = false;
	
	if ((modifiers & ALT_MASK) != 0)
		return false;
	
	if ((modifiers & COMMAND_MASK) != 0) {
		// move the cursor to the end of the buffer
		cursor = buffer->len;
	} else {
		// move the cursor to the end of the line
		cursor = CursorLineEnd (cursor);
	}
	
	if ((modifiers & SHIFT_MASK) == 0) {
		// clobber the selection
		anchor = cursor;
	}
	
	// check to see if selection has changed
	if (selection_anchor != anchor || selection_cursor != cursor) {
		SetSelectionStart (MIN (anchor, cursor));
		SetSelectionLength (abs (cursor - anchor));
		selection_anchor = anchor;
		selection_cursor = cursor;
		emit |= SELECTION_CHANGED;
		have_offset = false;
		handled = true;
	}
	
	return handled;
}

bool
TextBoxBase::KeyPressRight (MoonModifier modifiers)
{
	int anchor = selection_anchor;
	int cursor = selection_cursor;
	bool handled = false;
	
	if ((modifiers & ALT_MASK) != 0)
		return false;
	
	if ((modifiers & COMMAND_MASK) != 0) {
		// move the cursor to beginning of the next word
		cursor = CursorNextWord (cursor);
	} else if ((modifiers & SHIFT_MASK) == 0 && anchor != cursor) {
		// set cursor at end of selection
		cursor = MAX (anchor, cursor);
	} else {
		// move the cursor forward one character
		if (buffer->text[cursor] == '\r' && buffer->text[cursor + 1] == '\n') 
			cursor += 2;
		else if (cursor < buffer->len)
			cursor++;
	}
	
	if ((modifiers & SHIFT_MASK) == 0) {
		// clobber the selection
		anchor = cursor;
	}
	
	// check to see if selection has changed
	if (selection_anchor != anchor || selection_cursor != cursor) {
		SetSelectionStart (MIN (anchor, cursor));
		SetSelectionLength (abs (cursor - anchor));
		selection_anchor = anchor;
		selection_cursor = cursor;
		emit |= SELECTION_CHANGED;
		handled = true;
	}
	
	return handled;
}

bool
TextBoxBase::KeyPressLeft (MoonModifier modifiers)
{
	int anchor = selection_anchor;
	int cursor = selection_cursor;
	bool handled = false;
	
	if ((modifiers & ALT_MASK) != 0)
		return false;
	
	if ((modifiers & COMMAND_MASK) != 0) {
		// move the cursor to the beginning of the previous word
		cursor = CursorPrevWord (cursor);
	} else if ((modifiers & SHIFT_MASK) == 0 && anchor != cursor) {
		// set cursor at start of selection
		cursor = MIN (anchor, cursor);
	} else {
		// move the cursor backward one character
		if (cursor >= 2 && buffer->text[cursor - 2] == '\r' && buffer->text[cursor - 1] == '\n')
			cursor -= 2;
		else if (cursor > 0)
			cursor--;
	}
	
	if ((modifiers & SHIFT_MASK) == 0) {
		// clobber the selection
		anchor = cursor;
	}
	
	// check to see if selection has changed
	if (selection_anchor != anchor || selection_cursor != cursor) {
		SetSelectionStart (MIN (anchor, cursor));
		SetSelectionLength (abs (cursor - anchor));
		selection_anchor = anchor;
		selection_cursor = cursor;
		emit |= SELECTION_CHANGED;
		handled = true;
	}
	
	return handled;
}

bool
TextBoxBase::KeyPressUnichar (gunichar c)
{
	int length = abs (selection_cursor - selection_anchor);
	int start = MIN (selection_anchor, selection_cursor);
	int anchor = selection_anchor;
	int cursor = selection_cursor;
	TextBoxUndoAction *action;
	
	if ((max_length > 0 && buffer->len >= max_length) || ((c == '\r') && !accepts_return))
		return false;
	
	if (length > 0) {
		// replace the currently selected text
		action = new TextBoxUndoActionReplace (selection_anchor, selection_cursor, buffer, start, length, c);
		undo->Push (action);
		redo->Clear ();
		
		buffer->Replace (start, length, &c, 1);
	} else {
		// insert the text at the cursor position
		TextBoxUndoActionInsert *insert = NULL;
		
		if ((action = undo->Peek ()) && action->type == TextBoxUndoActionTypeInsert) {
			insert = (TextBoxUndoActionInsert *) action;
			
			if (!insert->Insert (start, c))
				insert = NULL;
		}
		
		if (!insert) {
			insert = new TextBoxUndoActionInsert (selection_anchor, selection_cursor, start, c);
			undo->Push (insert);
		}
		
		redo->Clear ();
		
		buffer->Insert (start, c);
	}
	
	emit |= TEXT_CHANGED;
	cursor = start + 1;
	anchor = cursor;
	
	// check to see if selection has changed
	if (selection_anchor != anchor || selection_cursor != cursor) {
		SetSelectionStart (MIN (anchor, cursor));
		SetSelectionLength (abs (cursor - anchor));
		selection_anchor = anchor;
		selection_cursor = cursor;
		emit |= SELECTION_CHANGED;
	}
	
	return true;
}

void
TextBoxBase::BatchPush ()
{
	batch++;
}

void
TextBoxBase::BatchPop ()
{
	if (batch == 0) {
		g_warning ("TextBoxBase batch underflow");
		return;
	}
	
	batch--;
}

void
TextBoxBase::SyncAndEmit (bool sync_text)
{
	if (batch != 0 || emit == NOTHING_CHANGED)
		return;
	
	if (sync_text && (emit & TEXT_CHANGED))
		SyncText ();
	
	if (emit & SELECTION_CHANGED)
		SyncSelectedText ();
	
	if (IsLoaded ()) {
		// eliminate events that we can't emit
		emit &= events_mask;
		
		if (emit & TEXT_CHANGED)
			EmitTextChanged ();
		
		if (emit & SELECTION_CHANGED)
			EmitSelectionChanged ();
	}
	
	emit = NOTHING_CHANGED;
}

void
TextBoxBase::Paste (MoonClipboard *clipboard, const char *str)
{
	int length = abs (selection_cursor - selection_anchor);
	int start = MIN (selection_anchor, selection_cursor);
	TextBoxUndoAction *action;
	gunichar *text;
	glong len, i;
	
	if (!(text = g_utf8_to_ucs4_fast (str ? str : "", -1, &len)))
		return;
	
	if (max_length > 0 && ((buffer->len - length) + len > max_length)) {
		// paste cannot exceed MaxLength
		len = max_length - (buffer->len - length);
		if (len > 0)
			text = (gunichar *) g_realloc (text, UNICODE_LEN (len + 1));
		else
			len = 0;
		text[len] = '\0';
	}
	
	if (!multiline) {
		// only paste the content of the first line
		for (i = 0; i < len; i++) {
			if (text[i] == '\r' || text[i] == '\n' || text[i] == 0x2028) {
				text = (gunichar *) g_realloc (text, UNICODE_LEN (i + 1));
				text[i] = '\0';
				len = i;
				break;
			}
		}
	}
	
	ResetIMContext ();
	
	if (length > 0) {
		// replace the currently selected text
		action = new TextBoxUndoActionReplace (selection_anchor, selection_cursor, buffer, start, length, text, len);
		
		buffer->Replace (start, length, text, len);
	} else if (len > 0) {
		// insert the text at the cursor position
		action = new TextBoxUndoActionInsert (selection_anchor, selection_cursor, start, text, len, true);
		
		buffer->Insert (start, text, len);
	} else {
		g_free (text);
		return;
	}
	
	undo->Push (action);
	redo->Clear ();
	g_free (text);
	
	emit |= TEXT_CHANGED;
	start += len;
	
	BatchPush ();
	SetSelectionStart (start);
	SetSelectionLength (0);
	BatchPop ();
	
	SyncAndEmit ();
}

void
TextBoxBase::paste (MoonClipboard *clipboard, const char *text, gpointer closure)
{
	((TextBoxBase *) closure)->Paste (clipboard, text);
}

void
TextBoxBase::OnKeyDown (KeyEventArgs *args)
{
	MoonKeyEvent *event = args->GetEvent ();
	MoonModifier modifiers = (MoonModifier) event->GetModifiers ();
	Key key = event->GetSilverlightKey ();
	MoonClipboard *clipboard;
	bool handled = false;
	
	if (event->IsModifier ())
		return;
	
	// set 'emit' to NOTHING_CHANGED so that we can figure out
	// what has chanegd after applying the changes that this
	// keypress will cause.
	emit = NOTHING_CHANGED;
	BatchPush ();
	
	switch (key) {
	case KeyBACKSPACE:
		if (is_read_only)
			break;
		
		handled = KeyPressBackSpace (modifiers);
		break;
	case KeyDELETE:
		if (is_read_only)
			break;
		
		if ((modifiers & (COMMAND_MASK | ALT_MASK | SHIFT_MASK)) == SHIFT_MASK) {
			// Shift+Delete => Cut
			if (!secret && (clipboard = GetClipboard (this, MoonClipboard_Clipboard))) {
				if (selection_cursor != selection_anchor) {
					// copy selection to the clipboard and then cut
					clipboard->SetText (GetSelectedText ());
				}
			}
			
			SetSelectedText ("");
			handled = true;
		} else {
			handled = KeyPressDelete (modifiers);
		}
		break;
	case KeyINSERT:
		if ((modifiers & (COMMAND_MASK | ALT_MASK | SHIFT_MASK)) == SHIFT_MASK) {
			// Shift+Insert => Paste
			if (is_read_only)
				break;
			
			if ((clipboard = GetClipboard (this, MoonClipboard_Clipboard))) {
				// paste clipboard contents to the buffer
				clipboard->AsyncGetText (TextBoxBase::paste, this);
			}
			
			handled = true;
		} else if ((modifiers & (COMMAND_MASK | ALT_MASK | SHIFT_MASK)) == COMMAND_MASK) {
			// Control+Insert => Copy
			if (!secret && (clipboard = GetClipboard (this, MoonClipboard_Clipboard))) {
				if (selection_cursor != selection_anchor) {
					// copy selection to the clipboard
					clipboard->SetText (GetSelectedText ());
				}
			}
			
			handled = true;
		}
		break;
	case KeyPAGEDOWN:
		handled = KeyPressPageDown (modifiers);
		break;
	case KeyPAGEUP:
		handled = KeyPressPageUp (modifiers);
		break;
	case KeyHOME:
		handled = KeyPressHome (modifiers);
		break;
	case KeyEND:
		handled = KeyPressEnd (modifiers);
		break;
	case KeyRIGHT:
		handled = KeyPressRight (modifiers);
		break;
	case KeyLEFT:
		handled = KeyPressLeft (modifiers);
		break;
	case KeyDOWN:
		handled = KeyPressDown (modifiers);
		break;
	case KeyUP:
		handled = KeyPressUp (modifiers);
		break;
	default:
		if ((modifiers & (COMMAND_MASK | ALT_MASK | SHIFT_MASK)) == COMMAND_MASK) {
			switch (key) {
			case KeyA:
				// Ctrl+A => Select All
				handled = true;
				SelectAll ();
				break;
			case KeyC:
				// Ctrl+C => Copy
				if (!secret && (clipboard = GetClipboard (this, MoonClipboard_Clipboard))) {
					if (selection_cursor != selection_anchor) {
						// copy selection to the clipboard
						clipboard->SetText (GetSelectedText ());
					}
				}
				
				handled = true;
				break;
			case KeyX:
				// Ctrl+X => Cut
				if (is_read_only)
					break;
				
				if (!secret && (clipboard = GetClipboard (this, MoonClipboard_Clipboard))) {
					if (selection_cursor != selection_anchor) {
						// copy selection to the clipboard and then cut
						clipboard->SetText (GetSelectedText());
					}
				}
				
				SetSelectedText ("");
				handled = true;
				break;
			case KeyV:
				// Ctrl+V => Paste
				if (is_read_only)
					break;
				
				if ((clipboard = GetClipboard (this, MoonClipboard_Clipboard))) {
					// paste clipboard contents to the buffer
					clipboard->AsyncGetText (TextBoxBase::paste, this);
				}
				
				handled = true;
				break;
			case KeyY:
				// Ctrl+Y => Redo
				if (!is_read_only) {
					handled = true;
					Redo ();
				}
				break;
			case KeyZ:
				// Ctrl+Z => Undo
				if (!is_read_only) {
					handled = true;
					Undo ();
				}
				break;
			default:
				// unhandled Control commands
				break;
			}
		}
		break;
	}
	
	if (handled) {
		args->SetHandled (handled);
		ResetIMContext ();
	}
	
	BatchPop ();
	
	SyncAndEmit ();
}

void
TextBoxBase::PostOnKeyDown (KeyEventArgs *args)
{
	MoonKeyEvent *event = args->GetEvent ();
	int key = event->GetSilverlightKey ();
	gunichar c;
	
	if (args->GetHandled ())
		return;
	
	// Note: we don't set Handled=true because anything we handle here, we
	// want to bubble up.
	if (!is_read_only && im_ctx->FilterKeyPress (event)) {
		need_im_reset = true;
		return;
	}
	
	if (is_read_only || event->IsModifier ())
		return;
	
	// set 'emit' to NOTHING_CHANGED so that we can figure out
	// what has changed after applying the changes that this
	// keypress will cause.
	emit = NOTHING_CHANGED;
	BatchPush ();
	
	switch (key) {
	case KeyENTER:
		KeyPressUnichar ('\r');
		break;
	default:
		if ((event->GetModifiers () & (COMMAND_MASK | ALT_MASK)) == 0) {
			// normal character input
			if ((c = event->GetUnicode ()))
				KeyPressUnichar (c);
		}
		break;
	}
	
	BatchPop ();
	
	SyncAndEmit ();
}

void
TextBoxBase::OnKeyUp (KeyEventArgs *args)
{
	if (!is_read_only) {
		if (im_ctx->FilterKeyPress (args->GetEvent()))
			need_im_reset = true;
	}
}

bool
TextBoxBase::DeleteSurrounding (int offset, int n_chars)
{
	const char *delete_start, *delete_end;
	const char *text = GetActualText ();
	int anchor = selection_anchor;
	int cursor = selection_cursor;
	TextBoxUndoAction *action;
	int start, length;
	
	if (is_read_only)
		return true;
	
	// get the utf-8 pointers so that we can use them to get gunichar offsets
	delete_start = g_utf8_offset_to_pointer (text, selection_cursor) + offset;
	delete_end = delete_start + n_chars;
	
	// get the character length/start index
	length = g_utf8_pointer_to_offset (delete_start, delete_end);
	start = g_utf8_pointer_to_offset (text, delete_start);
	
	if (length > 0) {
		action = new TextBoxUndoActionDelete (selection_anchor, selection_cursor, buffer, start, length);
		undo->Push (action);
		redo->Clear ();
		
		buffer->Cut (start, length);
		emit |= TEXT_CHANGED;
		anchor = start;
		cursor = start;
	}
	
	BatchPush ();
	
	// check to see if selection has changed
	if (selection_anchor != anchor || selection_cursor != cursor) {
		SetSelectionStart (MIN (anchor, cursor));
		SetSelectionLength (abs (cursor - anchor));
		selection_anchor = anchor;
		selection_cursor = cursor;
		emit |= SELECTION_CHANGED;
	}
	
	BatchPop ();
	
	SyncAndEmit ();
	
	return true;
}

gboolean
TextBoxBase::delete_surrounding (MoonIMContext *context, int offset, int n_chars, gpointer user_data)
{
	return ((TextBoxBase *) user_data)->DeleteSurrounding (offset, n_chars);
}

bool
TextBoxBase::RetrieveSurrounding ()
{
	const char *text = GetActualText ();
	const char *cursor;

	cursor = g_utf8_offset_to_pointer (text, selection_cursor);
	
	im_ctx->SetSurroundingText (text, -1, cursor - text);
	
	return true;
}

gboolean
TextBoxBase::retrieve_surrounding (MoonIMContext *context, gpointer user_data)
{
	return ((TextBoxBase *) user_data)->RetrieveSurrounding ();
}

void
TextBoxBase::Commit (const char *str)
{
	int length = abs (selection_cursor - selection_anchor);
	int start = MIN (selection_anchor, selection_cursor);
	TextBoxUndoAction *action;
	int anchor, cursor;
	gunichar *text;
	glong len, i;
	
	if (is_read_only)
		return;
	
	if (!(text = g_utf8_to_ucs4_fast (str ? str : "", -1, &len)))
		return;
	
	if (max_length > 0 && ((buffer->len - length) + len > max_length)) {
		// paste cannot exceed MaxLength
		len = max_length - (buffer->len - length);
		if (len > 0)
			text = (gunichar *) g_realloc (text, UNICODE_LEN (len + 1));
		else
			len = 0;
		text[len] = '\0';
	}
	
	if (!multiline) {
		// only paste the content of the first line
		for (i = 0; i < len; i++) {
			if (g_unichar_type (text[i]) == G_UNICODE_LINE_SEPARATOR) {
				text = (gunichar *) g_realloc (text, UNICODE_LEN (i + 1));
				text[i] = '\0';
				len = i;
				break;
			}
		}
	}
	
	if (length > 0) {
		// replace the currently selected text
		action = new TextBoxUndoActionReplace (selection_anchor, selection_cursor, buffer, start, length, text, len);
		undo->Push (action);
		redo->Clear ();
		
		buffer->Replace (start, length, text, len);
	} else if (len > 0) {
		// insert the text at the cursor position
		TextBoxUndoActionInsert *insert = NULL;
		
		buffer->Insert (start, text, len);
		
		if ((action = undo->Peek ()) && action->type == TextBoxUndoActionTypeInsert) {
			insert = (TextBoxUndoActionInsert *) action;
			
			if (!insert->Insert (start, text, len))
				insert = NULL;
		}
		
		if (!insert) {
			insert = new TextBoxUndoActionInsert (selection_anchor, selection_cursor, start, text, len);
			undo->Push (insert);
		}
		
		redo->Clear ();
	} else {
		g_free (text);
		return;
	}
	
	emit = TEXT_CHANGED;
	cursor = start + len;
	anchor = cursor;
	g_free (text);
	
	BatchPush ();
	
	// check to see if selection has changed
	if (selection_anchor != anchor || selection_cursor != cursor) {
		SetSelectionStart (MIN (anchor, cursor));
		SetSelectionLength (abs (cursor - anchor));
		selection_anchor = anchor;
		selection_cursor = cursor;
		emit |= SELECTION_CHANGED;
	}
	
	BatchPop ();
	
	SyncAndEmit ();
}

void
TextBoxBase::commit (MoonIMContext *context, const char *str, gpointer user_data)
{
	((TextBoxBase *) user_data)->Commit (str);
}

void
TextBoxBase::ResetIMContext ()
{
	if (need_im_reset) {
		im_ctx->Reset ();
		need_im_reset = false;
	}
}

void
TextBoxBase::OnMouseLeftButtonDown (MouseButtonEventArgs *args)
{
	double x, y;
	int cursor;
	
	args->SetHandled (true);
	Focus ();
	
	if (view) {
		args->GetPosition (view, &x, &y);
		
		cursor = view->GetCursorFromXY (x, y);
		
		ResetIMContext ();
		
		// Single-Click: cursor placement
		captured = CaptureMouse ();
		selecting = true;
		
		BatchPush ();
		emit = NOTHING_CHANGED;
		SetSelectionStart (cursor);
		SetSelectionLength (0);
		BatchPop ();
		
		SyncAndEmit ();
	}
}

void
TextBoxBase::OnMouseLeftButtonMultiClick (MouseButtonEventArgs *args)
{
	int cursor, start, end;
	double x, y;
	
	args->SetHandled (true);
	
	if (view) {
		args->GetPosition (view, &x, &y);
		
		cursor = view->GetCursorFromXY (x, y);
		
		ResetIMContext ();
		
		if (((MoonButtonEvent*)args->GetEvent())->GetNumberOfClicks () == 3) {
			// Note: Silverlight doesn't implement this, but to
			// be consistent with other TextEntry-type
			// widgets in Gtk+, we will.
			//
			// Triple-Click: select the line
			if (captured)
				ReleaseMouseCapture ();
			start = CursorLineBegin (cursor);
			end = CursorLineEnd (cursor, true);
			selecting = false;
			captured = false;
		} else {
			// Double-Click: select the word
			if (captured)
				ReleaseMouseCapture ();
			start = CursorPrevWord (cursor);
			end = CursorNextWord (cursor);
			selecting = false;
			captured = false;
		}
		
		BatchPush ();
		emit = NOTHING_CHANGED;
		SetSelectionStart (start);
		SetSelectionLength (end - start);
		BatchPop ();
		
		SyncAndEmit ();
	}
}

void
TextBoxBase::mouse_left_button_multi_click (EventObject *sender, EventArgs *args, gpointer closure)
{
	((TextBoxBase *) closure)->OnMouseLeftButtonMultiClick ((MouseButtonEventArgs *) args);
}

void
TextBoxBase::OnMouseLeftButtonUp (MouseButtonEventArgs *args)
{
	if (captured)
		ReleaseMouseCapture ();
	
	args->SetHandled (true);
	selecting = false;
	captured = false;
}

void
TextBoxBase::OnMouseMove (MouseEventArgs *args)
{
	int anchor = selection_anchor;
	int cursor = selection_cursor;
	MoonClipboard *clipboard;
	double x, y;
	
	if (selecting) {
		args->GetPosition (view, &x, &y);
		args->SetHandled (true);
		
		cursor = view->GetCursorFromXY (x, y);
		
		BatchPush ();
		emit = NOTHING_CHANGED;
		SetSelectionStart (MIN (anchor, cursor));
		SetSelectionLength (abs (cursor - anchor));
		selection_anchor = anchor;
		selection_cursor = cursor;
		BatchPop ();
		
		SyncAndEmit ();
		
		if (!secret && (clipboard = GetClipboard (this, MoonClipboard_Primary))) {
			// copy the selection to the primary clipboard
			clipboard->SetText (GetSelectedText ());
		}
	}
}

void
TextBoxBase::OnLostFocus (RoutedEventArgs *args)
{
	focused = false;
	
	if (view)
		view->OnLostFocus ();
	
	if (!is_read_only) {
		im_ctx->FocusOut ();
		need_im_reset = true;
	}
}

void
TextBoxBase::OnGotFocus (RoutedEventArgs *args)
{
	focused = true;
	
	if (view)
		view->OnGotFocus ();
	
	if (!is_read_only) {
		im_ctx->FocusIn ();
		need_im_reset = true;
	}
}

void
TextBoxBase::EmitCursorPositionChanged (double height, double x, double y)
{
	Emit (TextBoxBase::CursorPositionChangedEvent, new CursorPositionChangedEventArgs (height, x, y));
}

void
TextBoxBase::DownloaderComplete (Downloader *downloader)
{
	FontManager *manager = Deployment::GetCurrent ()->GetFontManager ();
	const char *path;
	const Uri *uri;
	
	uri = downloader->GetUri ();
	
	// If the downloaded file was a zip file, this'll get the path to the
	// extracted zip directory, else it will simply be the path to the
	// downloaded file.
	if (!(path = downloader->GetUnzippedPath ()))
		return;
	
	manager->AddResource (uri->ToString (), path);

	if (HasHandlers (ModelChangedEvent))
		Emit (ModelChangedEvent, new TextBoxModelChangedEventArgs (TextBoxModelChangedFont, NULL));
}

void
TextBoxBase::downloader_complete (EventObject *sender, EventArgs *calldata, gpointer closure)
{
	((TextBoxBase *) closure)->DownloaderComplete ((Downloader *) sender);
}

void
TextBoxBase::AddFontSource (Downloader *downloader)
{
	downloader->AddHandler (downloader->CompletedEvent, downloader_complete, this);
	g_ptr_array_add (downloaders, downloader);
	downloader->ref ();
	
	if (downloader->Started () || downloader->Completed ()) {
		if (downloader->Completed ())
			DownloaderComplete (downloader);
	} else {
		// This is what actually triggers the download
		downloader->Send ();
	}
}

void
TextBoxBase::AddFontResource (const char *resource)
{
	FontManager *manager = Deployment::GetCurrent ()->GetFontManager ();
	Application *application = Application::GetCurrent ();
	Downloader *downloader;
	char *path;
	Uri *uri;
	
	uri = Uri::Create (resource);
	
	if (!uri)
		return;

	if (!application || !(path = application->GetResourceAsPath (GetResourceBase(), uri))) {
		if (IsAttached () && (downloader = GetDeployment ()->CreateDownloader ())) {
			downloader->Open ("GET", resource, FontPolicy);
			AddFontSource (downloader);
			downloader->unref ();
		}
		
		delete uri;
		
		return;
	}
	
	manager->AddResource (resource, path);
	g_free (path);
	delete uri;
}

void
TextBoxBase::OnPropertyChanged (PropertyChangedEventArgs *args, MoonError *error)
{
	TextBoxModelChangeType changed = TextBoxModelChangedNothing;
	
	if (args->GetId () == Control::FontFamilyProperty) {
		FontFamily *family = args->GetNewValue () ? args->GetNewValue ()->AsFontFamily () : NULL;
		char **families, *fragment;
		int i;
		
		CleanupDownloaders ();
		
		if (family && family->source) {
			families = g_strsplit (family->source, ",", -1);
			for (i = 0; families[i]; i++) {
				g_strstrip (families[i]);
				if ((fragment = strchr (families[i], '#'))) {
					// the first portion of this string is the resource name...
					*fragment = '\0';
					AddFontResource (families[i]);
				}
			}
			g_strfreev (families);
		}
		
		font->SetFamily (family ? family->source : NULL);
		changed = TextBoxModelChangedFont;
	} else if (args->GetId () == Control::FontSizeProperty) {
		double size = args->GetNewValue()->AsDouble ();
		changed = TextBoxModelChangedFont;
		font->SetSize (size);
	} else if (args->GetId () == Control::FontStretchProperty) {
		FontStretches stretch = args->GetNewValue()->AsFontStretch()->stretch;
		changed = TextBoxModelChangedFont;
		font->SetStretch (stretch);
	} else if (args->GetId () == Control::FontStyleProperty) {
		FontStyles style = args->GetNewValue()->AsFontStyle ()->style;
		changed = TextBoxModelChangedFont;
		font->SetStyle (style);
	} else if (args->GetId () == Control::FontWeightProperty) {
		FontWeights weight = args->GetNewValue()->AsFontWeight ()->weight;
		changed = TextBoxModelChangedFont;
		font->SetWeight (weight);
	}
	
	if (changed != TextBoxModelChangedNothing && HasHandlers (ModelChangedEvent))
		Emit (ModelChangedEvent, new TextBoxModelChangedEventArgs (changed, args));
	
	if (args->GetProperty ()->GetOwnerType () != Type::TEXTBOXBASE) {
		Control::OnPropertyChanged (args, error);
		return;
	}
	
	NotifyListenersOfPropertyChange (args, error);
}

void
TextBoxBase::OnSubPropertyChanged (DependencyProperty *prop, DependencyObject *obj, PropertyChangedEventArgs *subobj_args)
{
	if (prop && (prop->GetId () == Control::BackgroundProperty ||
		     prop->GetId () == Control::ForegroundProperty)) {
		if (HasHandlers (ModelChangedEvent))
			Emit (ModelChangedEvent, new TextBoxModelChangedEventArgs (TextBoxModelChangedBrush));
		Invalidate ();
	}
	
	if (prop->GetOwnerType () != Type::TEXTBOXBASE)
		Control::OnSubPropertyChanged (prop, obj, subobj_args);
}

void
TextBoxBase::OnApplyTemplate ()
{
	contentElement = GetTemplateChild ("ContentElement");
	
	if (contentElement == NULL) {
		g_warning ("TextBoxBase::OnApplyTemplate: no ContentElement found");
		Control::OnApplyTemplate ();
		return;
	}
	
	if (view != NULL) {
		view->SetTextBox (NULL);
	}

	// Store the view on the managed peer and then drop the extra ref.
	view = MoonUnmanagedFactory::CreateTextBoxView ();
	view->unref ();

	view->SetEnableCursor (!is_read_only);
	view->SetTextBox (this);
	
	// Insert our TextBoxView
	if (contentElement->Is (Type::CONTENTPRESENTER)) {
		ContentPresenter *presenter = (ContentPresenter *) contentElement;
		
		presenter->SetValue (ContentPresenter::ContentProperty, Value (view));
	} else if (contentElement->Is (Type::CONTENTCONTROL)) {
		ContentControl *control = (ContentControl *) contentElement;
		
		control->SetValue (ContentControl::ContentProperty, Value (view));
	} else if (contentElement->Is (Type::BORDER)) {
		Border *border = (Border *) contentElement;
		
		border->SetValue (Border::ChildProperty, Value (view));
	} else if (contentElement->Is (Type::PANEL)) {
		DependencyObjectCollection *children = ((Panel *) contentElement)->GetChildren ();
		
		children->Add ((TextBoxView*) view);
	} else {
		g_warning ("TextBoxBase::OnApplyTemplate: don't know how to handle a ContentElement of type %s",
			   contentElement->GetType ()->GetName ());
		view->SetTextBox (NULL);
		view = NULL;
	}
	
	Control::OnApplyTemplate ();
}

void
TextBoxBase::ClearSelection (int start)
{
	BatchPush ();
	SetSelectionStart (start);
	SetSelectionLength (0);
	BatchPop ();
}

bool
TextBoxBase::SelectWithError (int start, int length, MoonError *error)
{
	if (start < 0) {
		MoonError::FillIn (error, MoonError::ARGUMENT, "selection start must be >= 0");
		return false;
	}

	if (length < 0) {
		MoonError::FillIn (error, MoonError::ARGUMENT, "selection length must be >= 0");
		return false;
	}

	if (start > buffer->len)
		start = buffer->len;
	
	if (length > (buffer->len - start))
		length = (buffer->len - start);
	
	BatchPush ();
	SetSelectionStart (start);
	SetSelectionLength (length);
	BatchPop ();
	
	ResetIMContext ();
	
	SyncAndEmit ();

	return true;
}

void
TextBoxBase::SelectAll ()
{
	SelectWithError (0, buffer->len, NULL);
}

bool
TextBoxBase::CanUndo ()
{
	return !undo->IsEmpty ();
}

bool
TextBoxBase::CanRedo ()
{
	return !redo->IsEmpty ();
}

void
TextBoxBase::Undo ()
{
	TextBoxUndoActionReplace *replace;
	TextBoxUndoActionInsert *insert;
	TextBoxUndoActionDelete *dele;
	TextBoxUndoAction *action;
	int anchor = 0, cursor = 0;
	
	if (undo->IsEmpty ())
		return;
	
	action = undo->Pop ();
	redo->Push (action);
	
	switch (action->type) {
	case TextBoxUndoActionTypeInsert:
		insert = (TextBoxUndoActionInsert *) action;
		
		buffer->Cut (insert->start, insert->length);
		anchor = action->selection_anchor;
		cursor = action->selection_cursor;
		break;
	case TextBoxUndoActionTypeDelete:
		dele = (TextBoxUndoActionDelete *) action;
		
		buffer->Insert (dele->start, dele->text, dele->length);
		anchor = action->selection_anchor;
		cursor = action->selection_cursor;
		break;
	case TextBoxUndoActionTypeReplace:
		replace = (TextBoxUndoActionReplace *) action;
		
		buffer->Cut (replace->start, replace->inlen);
		buffer->Insert (replace->start, replace->deleted, replace->length);
		anchor = action->selection_anchor;
		cursor = action->selection_cursor;
		break;
	}
	
	BatchPush ();
	SetSelectionStart (MIN (anchor, cursor));
	SetSelectionLength (abs (cursor - anchor));
	emit = TEXT_CHANGED | SELECTION_CHANGED;
	selection_anchor = anchor;
	selection_cursor = cursor;
	BatchPop ();
	
	SyncAndEmit ();
}

void
TextBoxBase::Redo ()
{
	TextBoxUndoActionReplace *replace;
	TextBoxUndoActionInsert *insert;
	TextBoxUndoActionDelete *dele;
	TextBoxUndoAction *action;
	int anchor = 0, cursor = 0;
	
	if (redo->IsEmpty ())
		return;
	
	action = redo->Pop ();
	undo->Push (action);
	
	switch (action->type) {
	case TextBoxUndoActionTypeInsert:
		insert = (TextBoxUndoActionInsert *) action;
		
		buffer->Insert (insert->start, insert->buffer->text, insert->buffer->len);
		anchor = cursor = insert->start + insert->buffer->len;
		break;
	case TextBoxUndoActionTypeDelete:
		dele = (TextBoxUndoActionDelete *) action;
		
		buffer->Cut (dele->start, dele->length);
		anchor = cursor = dele->start;
		break;
	case TextBoxUndoActionTypeReplace:
		replace = (TextBoxUndoActionReplace *) action;
		
		buffer->Cut (replace->start, replace->length);
		buffer->Insert (replace->start, replace->inserted, replace->inlen);
		anchor = cursor = replace->start + replace->inlen;
		break;
	}
	
	BatchPush ();
	SetSelectionStart (MIN (anchor, cursor));
	SetSelectionLength (abs (cursor - anchor));
	emit = TEXT_CHANGED | SELECTION_CHANGED;
	selection_anchor = anchor;
	selection_cursor = cursor;
	BatchPop ();
	
	SyncAndEmit ();
}


//
// TextBoxDynamicPropertyValueProvider
//

class TextBoxBaseDynamicPropertyValueProvider : public FrameworkElementProvider {
private:
	int foreground_id;
	int background_id;
	int baseline_offset_id;

	Value *selection_background;
	Value *selection_foreground;
	Value *baseline_offset;
public:
	virtual void RecomputePropertyValue (DependencyProperty *property, ProviderFlags flags, MoonError *error)
	{
		if (property->GetId () == background_id) {
			delete selection_background;
			selection_background = NULL;
		} else if (property->GetId () == foreground_id) {
			delete selection_foreground;
			selection_foreground = NULL;
		}

		FrameworkElementProvider::RecomputePropertyValue (property, flags, error);
	}

	TextBoxBaseDynamicPropertyValueProvider (DependencyObject *obj, PropertyPrecedence precedence, int foregroundId, int backgroundId, int baselineOffsetId)
		: FrameworkElementProvider (obj, precedence, ProviderFlags_RecomputesOnClear | ProviderFlags_RecomputesOnLowerPriorityChange)
	{
		foreground_id = foregroundId;
		background_id = backgroundId;
		baseline_offset_id = baselineOffsetId;
		selection_background = NULL;
		selection_foreground = NULL;
		baseline_offset = NULL;
	}
	
	virtual ~TextBoxBaseDynamicPropertyValueProvider ()
	{
		delete selection_background;
		delete selection_foreground;
		delete baseline_offset;
	}
	
	virtual Value *GetPropertyValue (DependencyProperty *property);
	
	void InitializeSelectionBrushes ()
	{
		if (!selection_background)
			selection_background = Value::CreateUnrefPtr (new SolidColorBrush ("#FF444444"));
		
		if (!selection_foreground)
			selection_foreground = Value::CreateUnrefPtr (new SolidColorBrush ("#FFFFFFFF"));
	}
};


Value *
TextBoxBaseDynamicPropertyValueProvider::GetPropertyValue (DependencyProperty *property)
	{
		// Check to see if a lower precedence (a style) has the value before
		// returning the dynamic one.
		Value *v = NULL;
		if (property->GetId () == background_id) {
			v = obj->GetValue (property, (PropertyPrecedence) (precedence + 1));
			if (!v)
				v = selection_background;
		} else if (property->GetId () == foreground_id) {
			v = obj->GetValue (property, (PropertyPrecedence) (precedence + 1));
			if (!v)
				v = selection_foreground;
		} else if (property->GetId () == baseline_offset_id) {
			delete baseline_offset;
			TextBoxView *view = ((TextBoxBase*)obj)->view;
			baseline_offset = new Value (view ? view->GetBaselineOffset () : 0);
			v = baseline_offset;
		}
		return v ? v : FrameworkElementProvider::GetPropertyValue (property);
	}

class TextBoxDynamicPropertyValueProvider : public TextBoxBaseDynamicPropertyValueProvider {
public:
	TextBoxDynamicPropertyValueProvider (DependencyObject *obj, PropertyPrecedence precedence )
		: TextBoxBaseDynamicPropertyValueProvider (obj, precedence,
							   TextBox::SelectionForegroundProperty, TextBox::SelectionBackgroundProperty, TextBox::BaselineOffsetProperty)
	{

	}
};


//
// TextBox
//

TextBox::TextBox ()
{
	delete providers.dynamicvalue;
	providers.dynamicvalue = new TextBoxDynamicPropertyValueProvider (this, PropertyPrecedence_DynamicValue);
	
	Initialize (Type::TEXTBOX);
	events_mask = TEXT_CHANGED | SELECTION_CHANGED;
	multiline = true;
}

void
TextBox::EmitSelectionChanged ()
{
	EmitAsync (TextBox::SelectionChangedEvent, MoonUnmanagedFactory::CreateRoutedEventArgs ());
}

void
TextBox::EmitTextChanged ()
{
	EmitAsync (TextBox::TextChangedEvent, MoonUnmanagedFactory::CreateTextChangedEventArgs ());
}

void
TextBox::SyncSelectedText ()
{
	if (selection_cursor != selection_anchor) {
		int length = abs (selection_cursor - selection_anchor);
		int start = MIN (selection_anchor, selection_cursor);
		char *text;
		
		text = g_ucs4_to_utf8 (buffer->text + start, length, NULL, NULL, NULL);
		
		setvalue = false;
		SetValue (TextBox::SelectedTextProperty, Value (text, Type::STRING, true));
		setvalue = true;
	} else {
		setvalue = false;
		SetSelectedText ("");
		setvalue = true;
	}
}

void
TextBox::SyncText ()
{
	char *text;
	
	text = g_ucs4_to_utf8 (buffer->text, buffer->len, NULL, NULL, NULL);
	
	setvalue = false;
	SetValue (TextBox::TextProperty, Value (text, Type::STRING, true));
	setvalue = true;
}

void
TextBox::OnPropertyChanged (PropertyChangedEventArgs *args, MoonError *error)
{
	TextBoxModelChangeType changed = TextBoxModelChangedNothing;
	DependencyProperty *prop;
	int start, length;
	
	if (args->GetProperty ()->GetOwnerType () != Type::TEXTBOX) {
		TextBoxBase::OnPropertyChanged (args, error);
		return;
	}
	
	if (args->GetId () == TextBox::AcceptsReturnProperty) {
		// update accepts_return state
		accepts_return = args->GetNewValue ()->AsBool ();
	} else if (args->GetId () == TextBox::CaretBrushProperty) {
		// FIXME: if we want to be perfect, we could invalidate the
		// blinking cursor rect if it is active... but is it that
		// important?
	} else if (args->GetId () == TextBox::FontSourceProperty) {
		FontSource *fs = args->GetNewValue () ? args->GetNewValue ()->AsFontSource () : NULL;
		FontManager *manager = Deployment::GetCurrent ()->GetFontManager ();
		
		// FIXME: ideally we'd remove the old item from the cache (or,
		// rather, 'unref' it since some other textblocks/boxes might
		// still be using it).
		
		delete font_resource;
		font_resource = NULL;
		
		if (fs != NULL) {
			switch (fs->type) {
			case FontSourceTypeManagedStream:
				font_resource = manager->AddResource (fs->source.stream);
				break;
			case FontSourceTypeGlyphTypeface:
				font_resource = new FontResource (fs->source.typeface);
				break;
			}
		}
		
		changed = TextBoxModelChangedFont;
		font->SetResource (font_resource);
	} else if (args->GetId () == TextBox::IsReadOnlyProperty) {
		// update is_read_only state
		is_read_only = args->GetNewValue ()->AsBool ();
		
		if (focused) {
			if (is_read_only) {
				ResetIMContext ();
				im_ctx->FocusOut ();
			} else {
				im_ctx->FocusIn ();
			}
		}
		
		if (view)
			view->SetEnableCursor (!is_read_only);
	} else if (args->GetId () == TextBox::MaxLengthProperty) {
		// update max_length state
		max_length = args->GetNewValue ()->AsInt32 ();
	} else if (args->GetId () == TextBox::SelectedTextProperty) {
		if (setvalue) {
			Value *value = args->GetNewValue ();
			const char *str = value && value->AsString () ? value->AsString () : "";
			TextBoxUndoAction *action = NULL;
			gunichar *text;
			glong textlen;
			
			length = abs (selection_cursor - selection_anchor);
			start = MIN (selection_anchor, selection_cursor);
			
			if ((text = g_utf8_to_ucs4_fast (str, -1, &textlen)))
			{
				if (length > 0) {
					// replace the currently selected text
					action = new TextBoxUndoActionReplace (selection_anchor, selection_cursor, buffer, start, length, text, textlen);
					
					buffer->Replace (start, length, text, textlen);
				} else if (textlen > 0) {
					// insert the text at the cursor
					action = new TextBoxUndoActionInsert (selection_anchor, selection_cursor, start, text, textlen);
					
					buffer->Insert (start, text, textlen);
				}
				
				g_free (text);
				
				if (action != NULL) {
					emit |= TEXT_CHANGED;
					undo->Push (action);
					redo->Clear ();
					
					ClearSelection (start + textlen);
					ResetIMContext ();
					
					SyncAndEmit ();
				}
			}
			else {
				g_warning ("g_utf8_to_ucs4_fast failed for string '%s'", str);
			}
		}
	} else if (args->GetId () == TextBox::SelectionStartProperty) {
		length = abs (selection_cursor - selection_anchor);
		start = args->GetNewValue ()->AsInt32 ();
		
		if (start > buffer->len) {
			// clamp the selection start offset to a valid value
			SetSelectionStart (buffer->len);
			return;
		}
		
		if (start + length > buffer->len) {
			// clamp the selection length to a valid value
			BatchPush ();
			length = buffer->len - start;
			SetSelectionLength (length);
			BatchPop ();
		}
		
		// SelectionStartProperty is marked as AlwaysChange -
		// if the value hasn't actually changed, then we do
		// not want to emit the TextBoxModelChanged event.
		if (selection_anchor != start) {
			changed = TextBoxModelChangedSelection;
			have_offset = false;
		}
		
		// When set programatically, anchor is always the
		// start and cursor is always the end.
		selection_cursor = start + length;
		selection_anchor = start;
		
		emit |= SELECTION_CHANGED;
		
		SyncAndEmit ();
	} else if (args->GetId () == TextBox::SelectionLengthProperty) {
		start = MIN (selection_anchor, selection_cursor);
		length = args->GetNewValue ()->AsInt32 ();
		
		if (start + length > buffer->len) {
			// clamp the selection length to a valid value
			length = buffer->len - start;
			SetSelectionLength (length);
			return;
		}
		
		// SelectionLengthProperty is marked as AlwaysChange -
		// if the value hasn't actually changed, then we do
		// not want to emit the TextBoxModelChanged event.
		if (selection_cursor != start + length) {
			changed = TextBoxModelChangedSelection;
			have_offset = false;
		}
		
		// When set programatically, anchor is always the
		// start and cursor is always the end.
		selection_cursor = start + length;
		selection_anchor = start;
		
		emit |= SELECTION_CHANGED;
		
		SyncAndEmit ();
	} else if (args->GetId () == TextBox::SelectionBackgroundProperty) {
		changed = TextBoxModelChangedBrush;
	} else if (args->GetId () == TextBox::SelectionForegroundProperty) {
		changed = TextBoxModelChangedBrush;
	} else if (args->GetId () == TextBox::TextProperty) {
		if (setvalue) {
			Value *value = args->GetNewValue ();
			const char *str = value && value->AsString () ? value->AsString () : "";
			TextBoxUndoAction *action;
			gunichar *text;
			glong textlen;
			
			if ((text = g_utf8_to_ucs4_fast (str, -1, &textlen)))
			{
				if (buffer->len > 0) {
					// replace the current text
					action = new TextBoxUndoActionReplace (selection_anchor, selection_cursor, buffer, 0, buffer->len, text, textlen);
					
					buffer->Replace (0, buffer->len, text, textlen);
				} else {
					// insert the text
					action = new TextBoxUndoActionInsert (selection_anchor, selection_cursor, 0, text, textlen);
					
					buffer->Insert (0, text, textlen);
				}
				
				undo->Push (action);
				redo->Clear ();
				g_free (text);
				
				emit |= TEXT_CHANGED;
				ClearSelection (0);
				ResetIMContext ();
				
				SyncAndEmit (false);
			}
			else {
				g_warning ("g_utf8_to_ucs4_fast failed for string '%s'", str);
			}
		}
		
		changed = TextBoxModelChangedText;
	} else if (args->GetId () == TextBox::TextAlignmentProperty) {
		changed = TextBoxModelChangedTextAlignment;
	} else if (args->GetId () == TextBox::TextWrappingProperty) {
		if (contentElement) {
			if ((prop = contentElement->GetDependencyProperty ("HorizontalScrollBarVisibility"))) {
				// If TextWrapping is set to Wrap, disable the horizontal scroll bars
				if (args->GetNewValue ()->AsTextWrapping () == TextWrappingWrap)
					contentElement->SetValue (prop, Value (ScrollBarVisibilityDisabled, Type::SCROLLBARVISIBILITY));
				else
					contentElement->SetValue (prop, GetValue (TextBox::HorizontalScrollBarVisibilityProperty));
			}
		}
		
		changed = TextBoxModelChangedTextWrapping;
	} else if (args->GetId () == TextBox::HorizontalScrollBarVisibilityProperty) {
		// XXX more crap because these aren't templatebound.
		if (contentElement) {
			if ((prop = contentElement->GetDependencyProperty ("HorizontalScrollBarVisibility"))) {
				// If TextWrapping is set to Wrap, disable the horizontal scroll bars
				if (GetTextWrapping () == TextWrappingWrap)
					contentElement->SetValue (prop, Value (ScrollBarVisibilityDisabled, Type::SCROLLBARVISIBILITY));
				else
					contentElement->SetValue (prop, args->GetNewValue ());
			}
		}
	} else if (args->GetId () == TextBox::VerticalScrollBarVisibilityProperty) {
		// XXX more crap because these aren't templatebound.
		if (contentElement) {
			if ((prop = contentElement->GetDependencyProperty ("VerticalScrollBarVisibility")))
				contentElement->SetValue (prop, args->GetNewValue ());
		}
	}
	
	if (changed != TextBoxModelChangedNothing && HasHandlers (ModelChangedEvent))
		Emit (ModelChangedEvent, new TextBoxModelChangedEventArgs (changed, args));
	
	NotifyListenersOfPropertyChange (args, error);
}

void
TextBox::OnSubPropertyChanged (DependencyProperty *prop, DependencyObject *obj, PropertyChangedEventArgs *subobj_args)
{
	if (prop && (prop->GetId () == TextBox::SelectionBackgroundProperty ||
		     prop->GetId () == TextBox::SelectionForegroundProperty)) {
		if (HasHandlers (ModelChangedEvent))
			Emit (ModelChangedEvent, new TextBoxModelChangedEventArgs (TextBoxModelChangedBrush));
		Invalidate ();
	}
	
	if (prop->GetOwnerType () != Type::TEXTBOX)
		TextBoxBase::OnSubPropertyChanged (prop, obj, subobj_args);
}

void
TextBox::OnApplyTemplate ()
{
	DependencyProperty *prop;
	
	TextBoxBase::OnApplyTemplate ();
	
	if (!contentElement)
		return;
	
	// XXX LAME these should be template bindings in the textbox template.
	if ((prop = contentElement->GetDependencyProperty ("VerticalScrollBarVisibility")))
		contentElement->SetValue (prop, GetValue (TextBox::VerticalScrollBarVisibilityProperty));
	
	if ((prop = contentElement->GetDependencyProperty ("HorizontalScrollBarVisibility"))) {
		// If TextWrapping is set to Wrap, disable the horizontal scroll bars
		if (GetTextWrapping () == TextWrappingWrap)
			contentElement->SetValue (prop, Value (ScrollBarVisibilityDisabled, Type::SCROLLBARVISIBILITY));
		else
			contentElement->SetValue (prop, GetValue (TextBox::HorizontalScrollBarVisibilityProperty));
	}
}


//
// PasswordBox
//

//
// PasswordBoxDynamicPropertyValueProvider
//

class PasswordBoxDynamicPropertyValueProvider : public TextBoxBaseDynamicPropertyValueProvider {
 public:
	PasswordBoxDynamicPropertyValueProvider (DependencyObject *obj, PropertyPrecedence precedence)
		: TextBoxBaseDynamicPropertyValueProvider (obj, precedence,
							   PasswordBox::SelectionForegroundProperty, PasswordBox::SelectionBackgroundProperty, PasswordBox::BaselineOffsetProperty)
	{
	}
};


//
// PasswordBox
//

PasswordBox::PasswordBox ()
{
	delete providers.dynamicvalue;
	providers.dynamicvalue = new PasswordBoxDynamicPropertyValueProvider (this, PropertyPrecedence_DynamicValue);
	
	Initialize (Type::PASSWORDBOX);
	events_mask = TEXT_CHANGED;
	secret = true;
	
	display = g_string_new ("");
}

PasswordBox::~PasswordBox ()
{
	g_string_free (display, true);
}

int
PasswordBox::CursorDown (int cursor, bool page)
{
	return GetBuffer ()->len;
}

int
PasswordBox::CursorUp (int cursor, bool page)
{
	return 0;
}

int
PasswordBox::CursorLineBegin (int cursor)
{
	return 0;
}

int
PasswordBox::CursorLineEnd (int cursor, bool include)
{
	return GetBuffer ()->len;
}

int
PasswordBox::CursorNextWord (int cursor)
{
	return GetBuffer ()->len;
}

int
PasswordBox::CursorPrevWord (int cursor)
{
	return 0;
}

void
PasswordBox::EmitTextChanged ()
{
	EmitAsync (PasswordBox::PasswordChangedEvent, MoonUnmanagedFactory::CreateRoutedEventArgs ());
}

void
PasswordBox::SyncSelectedText ()
{
	if (selection_cursor != selection_anchor) {
		int length = abs (selection_cursor - selection_anchor);
		int start = MIN (selection_anchor, selection_cursor);
		char *text;
		
		text = g_ucs4_to_utf8 (buffer->text + start, length, NULL, NULL, NULL);
		
		setvalue = false;
		SetValue (PasswordBox::SelectedTextProperty, Value (text, Type::STRING, true));
		setvalue = true;
	} else {
		setvalue = false;
		SetSelectedText ("");
		setvalue = true;
	}
}

void
PasswordBox::SyncDisplayText ()
{
	gunichar c = GetPasswordChar ();
	
	g_string_truncate (display, 0);
	
	for (int i = 0; i < buffer->len; i++)
		g_string_append_unichar (display, c);
}

void
PasswordBox::SyncText ()
{
	char *text;
	
	text = g_ucs4_to_utf8 (buffer->text, buffer->len, NULL, NULL, NULL);
	
	SyncDisplayText ();
	
	setvalue = false;
	SetValue (PasswordBox::PasswordProperty, Value (text, Type::STRING, true));
	setvalue = true;
}

const char *
PasswordBox::GetDisplayText ()
{
	return display->str;
}

void
PasswordBox::OnPropertyChanged (PropertyChangedEventArgs *args, MoonError *error)
{
	TextBoxModelChangeType changed = TextBoxModelChangedNothing;
	int length, start;
	
	if (args->GetProperty ()->GetOwnerType () != Type::PASSWORDBOX) {
		TextBoxBase::OnPropertyChanged (args, error);
		return;
	}
	
	if (args->GetId () == PasswordBox::CaretBrushProperty) {
		// FIXME: if we want to be perfect, we could invalidate the
		// blinking cursor rect if it is active... but is it that
		// important?
	} else if (args->GetId () == PasswordBox::FontSourceProperty) {
		FontSource *fs = args->GetNewValue () ? args->GetNewValue ()->AsFontSource () : NULL;
		FontManager *manager = Deployment::GetCurrent ()->GetFontManager ();
		
		// FIXME: ideally we'd remove the old item from the cache (or,
		// rather, 'unref' it since some other textblocks/boxes might
		// still be using it).
		
		delete font_resource;
		font_resource = NULL;
		
		if (fs != NULL) {
			switch (fs->type) {
			case FontSourceTypeManagedStream:
				font_resource = manager->AddResource (fs->source.stream);
				break;
			case FontSourceTypeGlyphTypeface:
				font_resource = new FontResource (fs->source.typeface);
				break;
			}
		}
		
		changed = TextBoxModelChangedFont;
		font->SetResource (font_resource);
	} else if (args->GetId () == PasswordBox::MaxLengthProperty) {
		// update max_length state
		max_length = args->GetNewValue()->AsInt32 ();
	} else if (args->GetId () == PasswordBox::PasswordCharProperty) {
		changed = TextBoxModelChangedText;
	} else if (args->GetId () == PasswordBox::PasswordProperty) {
		if (setvalue) {
			Value *value = args->GetNewValue ();
			const char *str = value && value->AsString () ? value->AsString () : "";
			TextBoxUndoAction *action;
			gunichar *text;
			glong textlen;
			
			if ((text = g_utf8_to_ucs4_fast (str, -1, &textlen)))
			{
				if (buffer->len > 0) {
					// replace the current text
					action = new TextBoxUndoActionReplace (selection_anchor, selection_cursor, buffer, 0, buffer->len, text, textlen);
					
					buffer->Replace (0, buffer->len, text, textlen);
				} else {
					// insert the text
					action = new TextBoxUndoActionInsert (selection_anchor, selection_cursor, 0, text, textlen);
					
					buffer->Insert (0, text, textlen);
				}
				
				undo->Push (action);
				redo->Clear ();
				g_free (text);

				Value *oldv = args->GetOldValue ();
				const char *olds = oldv && oldv->AsString () ? oldv->AsString () : "";
				// no PasswordChanged event is emitted if the password text has not changed (DRT #2003)
				if (strcmp (olds, str) != 0)
					emit |= TEXT_CHANGED;

				ClearSelection (0);
				ResetIMContext ();
				
				// Manually sync the DisplayText because we
				// don't want SyncAndEmit() to sync the
				// Password property again.
				SyncDisplayText ();
				
				SyncAndEmit (false);
			}
			else {
				g_warning ("g_utf8_to_ucs4_fast failed for string '%s'", str);
			}
		}
		
		changed = TextBoxModelChangedText;
	} else if (args->GetId () == PasswordBox::SelectedTextProperty) {
		if (setvalue) {
			Value *value = args->GetNewValue ();
			const char *str = value && value->AsString () ? value->AsString () : "";
			TextBoxUndoAction *action = NULL;
			gunichar *text;
			glong textlen;
			
			length = abs (selection_cursor - selection_anchor);
			start = MIN (selection_anchor, selection_cursor);
			
			if ((text = g_utf8_to_ucs4_fast (str, -1, &textlen)))
			{
				if (length > 0) {
					// replace the currently selected text
					action = new TextBoxUndoActionReplace (selection_anchor, selection_cursor, buffer, start, length, text, textlen);
					
					buffer->Replace (start, length, text, textlen);
				} else if (textlen > 0) {
					// insert the text at the cursor
					action = new TextBoxUndoActionInsert (selection_anchor, selection_cursor, start, text, textlen);
					
					buffer->Insert (start, text, textlen);
				}
				
				g_free (text);
				
				if (action != NULL) {
					undo->Push (action);
					redo->Clear ();
					
					ClearSelection (start + textlen);
					emit |= TEXT_CHANGED;
					SyncDisplayText ();
					ResetIMContext ();
					
					SyncAndEmit ();
				}
			}
			else {
				g_warning ("g_utf8_to_ucs4_fast failed for string '%s'", str);
			}
		}
	} else if (args->GetId () == PasswordBox::SelectionStartProperty) {
		length = abs (selection_cursor - selection_anchor);
		start = args->GetNewValue ()->AsInt32 ();
		
		if (start > buffer->len) {
			// clamp the selection start offset to a valid value
			SetSelectionStart (buffer->len);
			return;
		}
		
		if (start + length > buffer->len) {
			// clamp the selection length to a valid value
			BatchPush ();
			length = buffer->len - start;
			SetSelectionLength (length);
			BatchPop ();
		}
		
		// SelectionStartProperty is marked as AlwaysChange -
		// if the value hasn't actually changed, then we do
		// not want to emit the TextBoxModelChanged event.
		if (selection_anchor != start) {
			changed = TextBoxModelChangedSelection;
			have_offset = false;
		}
		
		// When set programatically, anchor is always the
		// start and cursor is always the end.
		selection_cursor = start + length;
		selection_anchor = start;
		
		emit |= SELECTION_CHANGED;
		
		SyncAndEmit ();
	} else if (args->GetId () == PasswordBox::SelectionLengthProperty) {
		start = MIN (selection_anchor, selection_cursor);
		length = args->GetNewValue ()->AsInt32 ();
		
		if (start + length > buffer->len) {
			// clamp the selection length to a valid value
			length = buffer->len - start;
			SetSelectionLength (length);
			return;
		}
		
		// SelectionLengthProperty is marked as AlwaysChange -
		// if the value hasn't actually changed, then we do
		// not want to emit the TextBoxModelChanged event.
		if (selection_cursor != start + length) {
			changed = TextBoxModelChangedSelection;
			have_offset = false;
		}
		
		// When set programatically, anchor is always the
		// start and cursor is always the end.
		selection_cursor = start + length;
		selection_anchor = start;
		
		emit |= SELECTION_CHANGED;
		
		SyncAndEmit ();
	} else if (args->GetId () == PasswordBox::SelectionBackgroundProperty) {
		changed = TextBoxModelChangedBrush;
	} else if (args->GetId () == PasswordBox::SelectionForegroundProperty) {
		changed = TextBoxModelChangedBrush;
	}
	
	if (changed != TextBoxModelChangedNothing && HasHandlers (ModelChangedEvent))
		Emit (ModelChangedEvent, new TextBoxModelChangedEventArgs (changed, args));
	
	NotifyListenersOfPropertyChange (args, error);
}

void
PasswordBox::OnSubPropertyChanged (DependencyProperty *prop, DependencyObject *obj, PropertyChangedEventArgs *subobj_args)
{
	if (prop && (prop->GetId () == PasswordBox::SelectionBackgroundProperty ||
		     prop->GetId () == PasswordBox::SelectionForegroundProperty)) {
		if (HasHandlers (ModelChangedEvent))
			Emit (ModelChangedEvent, new TextBoxModelChangedEventArgs (TextBoxModelChangedBrush));
		Invalidate ();
	}
	
	if (prop->GetOwnerType () != Type::TEXTBOX)
		TextBoxBase::OnSubPropertyChanged (prop, obj, subobj_args);
}


//
// TextBoxView
//

#define CURSOR_BLINK_ON_MULTIPLIER    4
#define CURSOR_BLINK_OFF_MULTIPLIER   2
#define CURSOR_BLINK_DELAY_MULTIPLIER 3
#define CURSOR_BLINK_DIVIDER          3

TextBoxView::TextBoxView ()
	: FrameworkElement (Type::TEXTBOXVIEW)
{
	AddHandler (UIElement::MouseLeftButtonDownEvent, TextBoxView::mouse_left_button_down, this);
	AddHandler (UIElement::MouseLeftButtonUpEvent, TextBoxView::mouse_left_button_up, this);
	
	SetCursor (CursorTypeIBeam);
	
	cursor = Rect (0, 0, 0, 0);
	layout = new TextLayout ();
	selection_changed = false;
	had_selected_text = false;
	cursor_visible = false;
	enable_cursor = true;
	blink_timeout = 0;
	textbox = NULL;
	dirty = false;
}

TextBoxView::~TextBoxView ()
{
	if (!GetDeployment ()->IsShuttingDown ())
		DisconnectBlinkTimeout ();
	
	delete layout;
}

TextLayoutLine *
TextBoxView::GetLineFromY (double y, int *index)
{
	return layout->GetLineFromY (Point (), y, index);
}

TextLayoutLine *
TextBoxView::GetLineFromIndex (int index)
{
	return layout->GetLineFromIndex (index);
}

int
TextBoxView::GetCursorFromXY (double x, double y)
{
	return layout->GetCursorFromXY (Point (), x, y);
}

bool
TextBoxView::blink (void *user_data)
{
	return ((TextBoxView *) user_data)->Blink ();
}

static guint
GetCursorBlinkTimeout (TextBoxView *view)
{
	MoonWindow *window;
	
	if (!view->IsAttached ())
		return CURSOR_BLINK_TIMEOUT_DEFAULT;
	
	if (!(window = view->GetDeployment ()->GetSurface ()->GetWindow ()))
		return CURSOR_BLINK_TIMEOUT_DEFAULT;
	
	return Runtime::GetWindowingSystem ()->GetCursorBlinkTimeout (window);
}

void
TextBoxView::ConnectBlinkTimeout (guint multiplier)
{
	guint timeout = GetCursorBlinkTimeout (this) * multiplier / CURSOR_BLINK_DIVIDER;
	TimeManager *manager;
	
	if (!IsAttached () || !(manager = GetDeployment ()->GetSurface ()->GetTimeManager ()))
		return;
	
	blink_timeout = manager->AddTimeout (MOON_PRIORITY_DEFAULT, timeout, TextBoxView::blink, this);
}

void
TextBoxView::DisconnectBlinkTimeout ()
{
	TimeManager *manager;
	
	if (blink_timeout != 0) {
		if (!IsAttached () || !(manager = GetDeployment ()->GetSurface ()->GetTimeManager ()))
			return;
		
		manager->RemoveTimeout (blink_timeout);
		blink_timeout = 0;
	}
}

bool
TextBoxView::Blink ()
{
	guint multiplier;
	
	SetCurrentDeployment (true);
	
	if (cursor_visible) {
		multiplier = CURSOR_BLINK_OFF_MULTIPLIER;
		HideCursor ();
	} else {
		multiplier = CURSOR_BLINK_ON_MULTIPLIER;
		ShowCursor ();
	}
	
	ConnectBlinkTimeout (multiplier);
	
	return false;
}

void
TextBoxView::DelayCursorBlink ()
{
	DisconnectBlinkTimeout ();
	ConnectBlinkTimeout (CURSOR_BLINK_DELAY_MULTIPLIER);
	UpdateCursor (true);
	ShowCursor ();
}

void
TextBoxView::BeginCursorBlink ()
{
	if (blink_timeout == 0) {
		ConnectBlinkTimeout (CURSOR_BLINK_ON_MULTIPLIER);
		UpdateCursor (true);
		ShowCursor ();
	}
}

void
TextBoxView::EndCursorBlink ()
{
	DisconnectBlinkTimeout ();
	
	if (cursor_visible)
		HideCursor ();
}

void
TextBoxView::ResetCursorBlink (bool delay)
{
	if (textbox->IsFocused () && !textbox->HasSelectedText ()) {
		if (enable_cursor) {
			// cursor is blinkable... proceed with blinkage
			if (delay)
				DelayCursorBlink ();
			else
				BeginCursorBlink ();
		} else {
			// cursor not blinkable, but we still need to keep track of it
			UpdateCursor (false);
		}
	} else {
		// cursor not blinkable... stop all blinkage
		EndCursorBlink ();
	}
}

void
TextBoxView::InvalidateCursor ()
{
	Invalidate (cursor.Transform (&absolute_xform));
}

void
TextBoxView::ShowCursor ()
{
	cursor_visible = true;
	InvalidateCursor ();
}

void
TextBoxView::HideCursor ()
{
	cursor_visible = false;
	InvalidateCursor ();
}

void
TextBoxView::UpdateCursor (bool invalidate)
{
	int cur = textbox->GetCursor ();
	Rect current = cursor;
	Rect rect;
	
	// invalidate current cursor rect
	if (invalidate && cursor_visible)
		InvalidateCursor ();
	
	// calculate the new cursor rect
	cursor = layout->GetCursor (Point (), cur);
	
	// transform the cursor rect into absolute coordinates for the IM context
	rect = cursor.Transform (&absolute_xform);
	
	textbox->im_ctx->SetCursorLocation (rect);
	
	if (cursor != current)
		textbox->EmitCursorPositionChanged (cursor.height, cursor.x, cursor.y);
	
	// invalidate the new cursor rect
	if (invalidate && cursor_visible)
		InvalidateCursor ();
}

void
TextBoxView::UpdateText ()
{
	const char *text = textbox->GetDisplayText ();
	
	layout->SetText (text ? text : "", -1);
}

void
TextBoxView::GetSizeForBrush (cairo_t *cr, double *width, double *height)
{
	*height = GetActualHeight ();
	*width = GetActualWidth ();
}

Size
TextBoxView::ComputeActualSize ()
{
	if (ReadLocalValue (LayoutInformation::LayoutSlotProperty))
		return FrameworkElement::ComputeActualSize ();

	Layout (Size (INFINITY, INFINITY));

	Size actual (0,0);
	layout->GetActualExtents (&actual.width, &actual.height);
       
	return actual;
}

Size
TextBoxView::MeasureOverrideWithError (Size availableSize, MoonError *error)
{
	Size desired = Size ();
	
	Layout (availableSize);
	
	layout->GetActualExtents (&desired.width, &desired.height);
	
	/* FIXME using a magic number for minumum width here */
	if (isinf (availableSize.width))
		desired.width = MAX (desired.width, 11);

	return desired.Min (availableSize);
}

Size
TextBoxView::ArrangeOverrideWithError (Size finalSize, MoonError *error)
{
	Size arranged = Size ();
	
	Layout (finalSize);
	
	layout->GetActualExtents (&arranged.width, &arranged.height);

	arranged = arranged.Max (finalSize);

	return arranged;
}

void
TextBoxView::Layout (Size constraint)
{
	layout->SetMaxWidth (constraint.width);
	
	layout->Layout ();
	dirty = false;
}

double
TextBoxView::GetBaselineOffset ()
{
	MoonError error;
	GeneralTransform *from_view_to_rtb = GetTransformToUIElementWithError (textbox, &error);

	Point p = from_view_to_rtb->Transform (Point (0,0));

	from_view_to_rtb->unref ();

	return layout->GetBaselineOffset () + p.y;
}

void
TextBoxView::Paint (cairo_t *cr)
{
	cairo_save (cr);
	if (GetFlowDirection () == FlowDirectionRightToLeft) {
		Rect bbox = layout->GetRenderExtents ();
		cairo_translate (cr, bbox.width, 0.0);
		cairo_scale (cr, -1.0, 1.0);
	}	
	layout->Render (cr, GetOriginPoint (), Point ());
	
	if (cursor_visible) {
		cairo_antialias_t alias = cairo_get_antialias (cr);
		Brush *caret = textbox->GetCaretBrush ();
		double h = round (cursor.height);
		double x = cursor.x;
		double y = cursor.y;
		
		// disable antialiasing
		cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
		
		// snap 'x' to the half-pixel grid (to try and get a sharp 1-pixel-wide line)
		cairo_user_to_device (cr, &x, &y);
		x = trunc (x) + 0.5; y = trunc (y);
		cairo_device_to_user (cr, &x, &y);
		
		// set the cursor color
		bool unref = false;
		if (!caret) {
			caret = new SolidColorBrush ("Black");
			unref = true;
		}
		caret->SetupBrush (cr, cursor);
		
		// draw the cursor
		cairo_set_line_width (cr, 1.0);
		cairo_move_to (cr, x, y);
		cairo_line_to (cr, x, y + h);
		
		// stroke the caret
		caret->Stroke (cr);
		
		if (unref)
			caret->unref ();

		// restore antialiasing
		cairo_set_antialias (cr, alias);
	}
	cairo_restore (cr);
}

void
TextBoxView::Render (Context *ctx, Region *region)
{
	cairo_t *cr = ctx->Push (Context::Cairo ());
	Render (cr, region);
	ctx->Pop ();
}

void
TextBoxView::Render (cairo_t *cr, Region *region, bool path_only)
{
	Size renderSize = GetRenderSize ();

	((TextBoxDynamicPropertyValueProvider *)textbox->providers.dynamicvalue)->InitializeSelectionBrushes ();
	
	UpdateCursor (false);
	
	if (selection_changed) {
		layout->Select (textbox->GetSelectionStart (), textbox->GetSelectionLength ());
		selection_changed = false;
	}
	
	cairo_save (cr);
	
	if (!path_only)
		RenderLayoutClip (cr);
	
	layout->SetAvailableWidth (renderSize.width);
	Paint (cr);
	cairo_restore (cr);
}

void
TextBoxView::OnModelChanged (TextBoxModelChangedEventArgs *args)
{
	switch (args->changed) {
	case TextBoxModelChangedTextAlignment:
		// text alignment changed, update our layout
		if (layout->SetTextAlignment (args->property->GetNewValue()->AsTextAlignment ()))
			dirty = true;
		break;
	case TextBoxModelChangedTextWrapping:
		// text wrapping changed, update our layout
		if (layout->SetTextWrapping (args->property->GetNewValue()->AsTextWrapping ()))
			dirty = true;
		break;
	case TextBoxModelChangedSelection:
		if (had_selected_text || textbox->HasSelectedText ()) {
			// the selection has changed, update the layout's selection
			had_selected_text = textbox->HasSelectedText ();
			selection_changed = true;
			ResetCursorBlink (false);
		} else {
			// cursor position changed
			ResetCursorBlink (true);
			return;
		}
		break;
	case TextBoxModelChangedBrush:
		// a brush has changed, no layout updates needed, we just need to re-render
		break;
	case TextBoxModelChangedFont:
		// font changed, need to recalculate layout/bounds
		layout->ResetState ();
		dirty = true;
		break;
	case TextBoxModelChangedText:
		// the text has changed, need to recalculate layout/bounds
		UpdateText ();
		dirty = true;
		break;
	default:
		// nothing changed??
		return;
	}
	
	if (dirty) {
		InvalidateMeasure ();
		UpdateBounds (true);
	}

	Invalidate ();
}

void
TextBoxView::model_changed (EventObject *sender, EventArgs *args, gpointer closure)
{
	((TextBoxView *) closure)->OnModelChanged ((TextBoxModelChangedEventArgs *) args);
}

void
TextBoxView::OnLostFocus ()
{
	EndCursorBlink ();
}

void
TextBoxView::OnGotFocus ()
{
	ResetCursorBlink (false);
}

void
TextBoxView::OnMouseLeftButtonDown (MouseButtonEventArgs *args)
{
	// proxy to our parent TextBox control
	textbox->OnMouseLeftButtonDown (args);
}

void
TextBoxView::mouse_left_button_down (EventObject *sender, EventArgs *args, gpointer closure)
{
	((TextBoxView *) closure)->OnMouseLeftButtonDown ((MouseButtonEventArgs *) args);
}

void
TextBoxView::OnMouseLeftButtonUp (MouseButtonEventArgs *args)
{
	// proxy to our parent TextBox control
	textbox->OnMouseLeftButtonUp (args);
}

void
TextBoxView::mouse_left_button_up (EventObject *sender, EventArgs *args, gpointer closure)
{
	((TextBoxView *) closure)->OnMouseLeftButtonUp ((MouseButtonEventArgs *) args);
}

void
TextBoxView::SetTextBox (TextBoxBase *textbox)
{
	TextLayoutAttributes *attrs;
	
	if (this->textbox == textbox)
		return;
	
	if (this->textbox) {
		// remove the event handlers from the old textbox
		this->textbox->RemoveHandler (TextBoxBase::ModelChangedEvent, TextBoxView::model_changed, this);
	}
	
	this->textbox = textbox;
	
	if (textbox) {
		textbox->AddHandler (TextBoxBase::ModelChangedEvent, TextBoxView::model_changed, this);
		
		// sync our state with the textbox
		layout->SetTextAttributes (new List ());
		attrs = new TextLayoutAttributes ((ITextAttributes *) textbox, 0);
		layout->GetTextAttributes ()->Append (attrs);
		
		layout->SetTextAlignment (textbox->GetTextAlignment ());
		layout->SetTextWrapping (textbox->GetTextWrapping ());
		had_selected_text = textbox->HasSelectedText ();
		selection_changed = true;
		UpdateText ();
	} else {
		layout->SetTextAttributes (NULL);
		layout->SetText (NULL, -1);
	}
	
	UpdateBounds (true);
	InvalidateMeasure ();
	Invalidate ();
	dirty = true;
}

void
TextBoxView::SetEnableCursor (bool enable)
{
	if ((enable_cursor && enable) || (!enable_cursor && !enable))
		return;
	
	enable_cursor = enable;
	
	if (enable)
		ResetCursorBlink (false);
	else
		EndCursorBlink ();
}

};
