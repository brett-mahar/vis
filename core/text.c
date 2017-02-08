#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include "tap.h"
#include "text.h"
#include "text-util.h"

#ifndef BUFSIZ
#define BUFSIZ 1024
#endif

static bool insert(Text *txt, size_t pos, const char *data) {
	return text_insert(txt, pos, data, strlen(data));
}

static bool isempty(Text *txt) {
	return text_size(txt) == 0;
}

static bool compare_iterator_forward(Iterator *it, const char *data) {
	char buf[BUFSIZ] = "", b;
	while (text_iterator_byte_get(it, &b)) {
		buf[it->pos] = b;
		text_iterator_byte_next(it, NULL);
	}
	return strcmp(data, buf) == 0;
}

static bool compare_iterator_backward(Iterator *it, const char *data) {
	char buf[BUFSIZ] = "", b;
	while (text_iterator_byte_get(it, &b)) {
		buf[it->pos] = b;
		text_iterator_byte_prev(it, NULL);
	}
	return strcmp(data, buf) == 0;
}

static bool compare_iterator_both(Text *txt, const char *data) {
	Iterator it = text_iterator_get(txt, 0);
	bool forward = compare_iterator_forward(&it, data);
	text_iterator_byte_prev(&it, NULL);
	bool forward_backward = compare_iterator_backward(&it, data);
	it = text_iterator_get(txt, text_size(txt));
	bool backward = compare_iterator_backward(&it, data);
	text_iterator_byte_next(&it, NULL);
	bool backward_forward = compare_iterator_forward(&it, data);
	return forward && backward && forward_backward && backward_forward;
}

static bool compare(Text *txt, const char *data) {
	char buf[BUFSIZ];
	size_t len = text_bytes_get(txt, 0, sizeof(buf)-1, buf);
	buf[len] = '\0';
	return len == strlen(data) && strcmp(buf, data) == 0 &&
	       compare_iterator_both(txt, data);
}

int main(int argc, char *argv[]) {
	Text *txt;

	plan_no_plan();

	skip_if(TIS_INTERPRETER, 2, "I/O related") {
		txt = text_load("/");
		ok(txt == NULL && errno == EISDIR, "Opening directory");

		if (access("/etc/shadow", F_OK) == 0) {
			txt = text_load("/etc/shadow");
			ok(txt == NULL && errno == EACCES, "Opening file without sufficient permissions");
		}
	}

	txt = text_load(NULL);
	ok(txt != NULL && isempty(txt), "Opening empty file");

	Iterator it = text_iterator_get(txt, 0);
	ok(text_iterator_valid(&it) && it.pos == 0, "Iterator on empty file");
	char b = '_';
	ok(text_iterator_byte_get(&it, &b) && b == '\0', "Read EOF from iterator of empty file");
	b = '_';
	ok(!text_iterator_byte_prev(&it, &b) && b == '_' &&
	   !text_iterator_valid(&it), "Moving iterator beyond start of file");
	ok(!text_iterator_byte_get(&it, &b) && b == '_' &&
	   !text_iterator_valid(&it), "Access iterator beyond start of file");
	ok(text_iterator_byte_next(&it, &b) && b == '\0' &&
	   text_iterator_valid(&it), "Moving iterator back from beyond start of file");
	b = '_';
	ok(text_iterator_byte_get(&it, &b) && b == '\0' &&
	   text_iterator_valid(&it), "Accessing iterator after moving back from beyond start of file");
	b = '_';
	ok(!text_iterator_byte_next(&it, &b) && b == '_' &&
	   !text_iterator_valid(&it), "Moving iterator beyond end of file");
	ok(!text_iterator_byte_get(&it, &b) && b == '_' &&
	   !text_iterator_valid(&it), "Accessing iterator beyond end of file");
	ok(text_iterator_byte_prev(&it, &b) && b == '\0' &&
	   text_iterator_valid(&it), "Moving iterator back from beyond end of file");
	b = '_';
	ok(text_iterator_byte_get(&it, &b) && b == '\0' &&
	   text_iterator_valid(&it), "Accessing iterator after moving back from beyond start of file");

	ok(insert(txt, 1, "") && isempty(txt), "Inserting empty data");
	ok(!insert(txt, 1, " ") && isempty(txt), "Inserting with invalid offset");

	/* test cached insertion (i.e. in-place with only one piece) */
	ok(insert(txt, 0, "3") && compare(txt, "3"), "Inserting into empty document (cached)");
	ok(insert(txt, 0, "1") && compare(txt, "13"), "Inserting at begin (cached)");
	ok(insert(txt, 1, "2") && compare(txt, "123"), "Inserting in middle (cached)");
	ok(insert(txt, text_size(txt), "4") && compare(txt, "1234"), "Inserting at end (cached)");

	ok(text_delete(txt, text_size(txt), 0) && compare(txt, "1234"), "Deleting empty range");
	ok(!text_delete(txt, text_size(txt), 1) && compare(txt, "1234"), "Deleting invalid offset");
	ok(!text_delete(txt, 0, text_size(txt)+5) && compare(txt, "1234"), "Deleting invalid range");

	ok(text_undo(txt) == 0 && compare(txt, ""), "Reverting to empty document");
	ok(text_redo(txt) != EPOS /* == text_size(txt) */ && compare(txt, "1234"), "Restoring previsous content");

	/* test cached deletion (i.e. in-place with only one piece) */
	ok(text_delete(txt, text_size(txt)-1, 1) && compare(txt, "123"), "Deleting at end (cached)");
	ok(text_delete(txt, 1, 1) && compare(txt, "13"), "Deleting in middle (cached)");
	ok(text_delete(txt, 0, 1) && compare(txt, "3"), "Deleting at begin (cached)");
	ok(text_delete(txt, 0, 1) && compare(txt, ""), "Deleting to empty document (cached)");

	/* test regular insertion (i.e. with multiple pieces) */
	text_snapshot(txt);
	ok(insert(txt, 0, "3") && compare(txt, "3"), "Inserting into empty document");
	text_snapshot(txt);
	ok(insert(txt, 0, "1") && compare(txt, "13"), "Inserting at begin");
	text_snapshot(txt);
	ok(insert(txt, 1, "2") && compare(txt, "123"), "Inserting in between");
	text_snapshot(txt);
	ok(insert(txt, text_size(txt), "46") && compare(txt, "12346"), "Inserting at end");
	text_snapshot(txt);
	ok(insert(txt, 4, "5") && compare(txt, "123456"), "Inserting in middle");
	text_snapshot(txt);
	ok(insert(txt, text_size(txt), "789") && compare(txt, "123456789"), "Inserting at end");
	text_snapshot(txt);
	ok(insert(txt, text_size(txt), "0") && compare(txt, "1234567890"), "Inserting at end");

	/* test simple undo / redo oparations */
	ok(text_undo(txt) != EPOS && compare(txt, "123456789"), "Undo 1");
	ok(text_undo(txt) != EPOS && compare(txt, "123456"), "Undo 2");
	ok(text_undo(txt) != EPOS && compare(txt, "12346"), "Undo 3");
	ok(text_undo(txt) != EPOS && compare(txt, "123"), "Undo 3");
	ok(text_undo(txt) != EPOS && compare(txt, "13"), "Undo 5");
	ok(text_undo(txt) != EPOS && compare(txt, "3"), "Undo 6");
	ok(text_undo(txt) != EPOS && compare(txt, ""), "Undo 6");
	ok(text_redo(txt) != EPOS && compare(txt, "3"), "Redo 1");
	ok(text_redo(txt) != EPOS && compare(txt, "13"), "Redo 2");
	ok(text_redo(txt) != EPOS && compare(txt, "123"), "Redo 3");
	ok(text_redo(txt) != EPOS && compare(txt, "12346"), "Redo 4");
	ok(text_redo(txt) != EPOS && compare(txt, "123456"), "Redo 5");
	ok(text_redo(txt) != EPOS && compare(txt, "123456789"), "Redo 6");
	ok(text_redo(txt) != EPOS && compare(txt, "1234567890"), "Redo 7");

	/* test regular deletion (i.e. with multiple pieces) */
	ok(text_delete(txt, 8, 2) && compare(txt, "12345678"), "Deleting midway start");
	text_undo(txt);
	ok(text_delete(txt, 2, 6) && compare(txt, "1290"), "Deleting midway end");
	text_undo(txt);
	ok(text_delete(txt, 7, 1) && compare(txt, "123456790"), "Deleting midway both same piece");
	text_undo(txt);
	ok(text_delete(txt, 0, 5) && compare(txt, "67890"), "Deleting at begin");
	text_undo(txt);
	ok(text_delete(txt, 5, 5) && compare(txt, "12345"), "Deleting at end");

	ok(text_mark_get(txt, text_mark_set(txt, -1)) == EPOS, "Mark invalid 1");
	ok(text_mark_get(txt, text_mark_set(txt, text_size(txt)+1)) == EPOS, "Mark invalid 2");
	Mark bof = text_mark_set(txt, 0);
	ok(text_mark_get(txt, bof) == 0, "Mark at beginning of file");
	size_t pos = 3;
	Mark mof = text_mark_set(txt, pos);
	ok(text_mark_get(txt, mof) == pos, "Mark in the middle");
	Mark eof = text_mark_set(txt, text_size(txt));
	ok(text_mark_get(txt, eof) == text_size(txt), "Mark at end of file");
	const char *chunk = "new content";
	size_t newpos = pos+strlen(chunk);
	ok(insert(txt, pos-1, chunk), "Insert before mark");
	ok(text_mark_get(txt, bof) == 0, "Mark at beginning adjusted 1");
	ok(text_mark_get(txt, mof) == newpos, "Mark in the middle adjusted 1");
	ok(text_mark_get(txt, eof) == text_size(txt), "Mark at end adjusted 1");
	ok(insert(txt, newpos+1, chunk), "Insert after mark");
	ok(text_mark_get(txt, bof) == 0, "Mark at beginning adjusted 2");
	ok(text_mark_get(txt, mof) == newpos, "Mark in the middle adjusted 2");
	ok(text_mark_get(txt, eof) == text_size(txt), "Mark at end adjusted 2");
	text_snapshot(txt);
	ok(text_delete(txt, newpos, 1), "Deleting mark");
	ok(text_mark_get(txt, mof) == EPOS, "Mark in the middle deleted");
	text_undo(txt);
	ok(text_mark_get(txt, mof) == newpos, "Mark restored");
	text_free(txt);

	return exit_status();
}
