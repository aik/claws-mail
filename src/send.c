/*
 * Sylpheed -- a GTK+ based, lightweight, and fast e-mail client
 * Copyright (C) 1999-2002 Hiroyuki Yamamoto
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "defs.h"

#include <glib.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkclist.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>

#include "intl.h"
#include "send.h"
#include "socket.h"
#include "ssl.h"
#include "smtp.h"
#include "prefs_common.h"
#include "prefs_account.h"
#include "account.h"
#include "compose.h"
#include "progressdialog.h"
#include "inputdialog.h"
#include "manage_window.h"
#include "procmsg.h"
#include "procheader.h"
#include "utils.h"
#include "gtkutils.h"
#include "statusbar.h"
#include "inc.h"
#include "log.h"

typedef struct _SendProgressDialog	SendProgressDialog;

struct _SendProgressDialog
{
	ProgressDialog *dialog;
	GList *queue_list;
	gboolean cancelled;
};
#if 0
static gint send_message_local		(const gchar		*command,
					 FILE			*fp);

static gint send_message_smtp		(PrefsAccount		*ac_prefs,
					 GSList			*to_list,
					 FILE			*fp);
#endif
static gint send_message_data		(SendProgressDialog	*dialog,
					 SockInfo		*sock,
					 FILE			*fp,
					 gint			 size);

static SendProgressDialog *send_progress_dialog_create (void);
static void send_progress_dialog_destroy	(SendProgressDialog *dialog);
static void send_cancel				(GtkWidget	    *widget,
						 gpointer	     data);

static void send_progress_dialog_update		(Session	    *session,
						 SMTPPhase	     phase);


gint send_message(const gchar *file, PrefsAccount *ac_prefs, GSList *to_list)
{
	FILE *fp;
	gint val;

	g_return_val_if_fail(file != NULL, -1);
	g_return_val_if_fail(ac_prefs != NULL, -1);
	g_return_val_if_fail(to_list != NULL, -1);

	if ((fp = fopen(file, "rb")) == NULL) {
		FILE_OP_ERROR(file, "fopen");
		return -1;
	}

	if (ac_prefs->use_mail_command && ac_prefs->mail_command &&
	    (*ac_prefs->mail_command)) {
		val = send_message_local(ac_prefs->mail_command, fp);
		fclose(fp);
		return val;
	}
	else if (prefs_common.use_extsend && prefs_common.extsend_cmd) {
		val = send_message_local(prefs_common.extsend_cmd, fp);
		fclose(fp);
		return val;
	}
	else {
		val = send_message_smtp(ac_prefs, to_list, fp);
		
		fclose(fp);
		return val;
	}
}

enum
{
	Q_SENDER     = 0,
	Q_SMTPSERVER = 1,
	Q_RECIPIENTS = 2,
	Q_ACCOUNT_ID = 3
};

#if 0
gint send_message_queue(const gchar *file)
{
	static HeaderEntry qentry[] = {{"S:",   NULL, FALSE},
				       {"SSV:", NULL, FALSE},
				       {"R:",   NULL, FALSE},
				       {"AID:", NULL, FALSE},
				       {NULL,   NULL, FALSE}};
	FILE *fp;
	gint val = 0;
	gchar *from = NULL;
	gchar *server = NULL;
	GSList *to_list = NULL;
	gchar buf[BUFFSIZE];
	gint hnum;
	glong fpos;
	PrefsAccount *ac = NULL, *mailac = NULL, *newsac = NULL;

	g_return_val_if_fail(file != NULL, -1);

	if ((fp = fopen(file, "rb")) == NULL) {
		FILE_OP_ERROR(file, "fopen");
		return -1;
	}

	while ((hnum = procheader_get_one_field(buf, sizeof(buf), fp, qentry))
	       != -1) {
		gchar *p = buf + strlen(qentry[hnum].name);

		switch (hnum) {
		case Q_SENDER:
			if (!from) from = g_strdup(p);
			break;
		case Q_SMTPSERVER:
			if (!server) server = g_strdup(p);
			break;
		case Q_RECIPIENTS:
			to_list = address_list_append(to_list, p);
			break;
		case Q_ACCOUNT_ID:
			ac = account_find_from_id(atoi(p));
			break;
		default:
			break;
		}
	}

	if (((!ac || (ac && ac->protocol != A_NNTP)) && !to_list) || !from) {
		g_warning(_("Queued message header is broken.\n"));
		val = -1;
	} else if (prefs_common.use_extsend && prefs_common.extsend_cmd) {
		val = send_message_local(prefs_common.extsend_cmd, fp);
	} else {
		if (ac && ac->protocol == A_NNTP) {
			newsac = ac;

			/* search mail account */
			mailac = account_find_from_address(from);
			if (!mailac) {
				if (cur_account &&
				    cur_account->protocol != A_NNTP)
					mailac = cur_account;
				else {
					mailac = account_get_default();
					if (mailac->protocol == A_NNTP)
						mailac = NULL;
				}
			}
		} else if (ac) {
			mailac = ac;
		} else {
			ac = account_find_from_smtp_server(from, server);
			if (!ac) {
				g_warning(_("Account not found. "
					    "Using current account...\n"));
				ac = cur_account;
				if (ac && ac->protocol != A_NNTP)
					mailac = ac;
			}
		}

		fpos = ftell(fp);
		if (to_list) {
			if (mailac)
				val = send_message_smtp(mailac, to_list, fp);
			else {
				PrefsAccount tmp_ac;

				g_warning(_("Account not found.\n"));

				memset(&tmp_ac, 0, sizeof(PrefsAccount));
				tmp_ac.address = from;
				tmp_ac.smtp_server = server;
				tmp_ac.smtpport = SMTP_PORT;
				val = send_message_smtp(&tmp_ac, to_list, fp);
			}
		}
		if (val == 0 && newsac) {
			fseek(fp, fpos, SEEK_SET);
			val = news_post_stream(FOLDER(newsac->folder), fp);
		}
	}

	slist_free_strings(to_list);
	g_slist_free(to_list);
	g_free(from);
	g_free(server);
	fclose(fp);

	return val;
}
#endif

gint send_message_local(const gchar *command, FILE *fp)
{
	FILE *pipefp;
	gchar buf[BUFFSIZE];
	int r;
	sigset_t osig, mask;

	g_return_val_if_fail(command != NULL, -1);
	g_return_val_if_fail(fp != NULL, -1);

	pipefp = popen(command, "w");
	if (!pipefp) {
		g_warning(_("Can't execute external command: %s\n"), command);
		return -1;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		strretchomp(buf);
		if (buf[0] == '.' && buf[1] == '\0')
			fputc('.', pipefp);
		fputs(buf, pipefp);
		fputc('\n', pipefp);
	}

	/* we need to block SIGCHLD, otherwise pspell's handler will wait()
	 * the pipecommand away and pclose will return -1 because of its
	 * failed wait4().
	 */
	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &mask, &osig);
	
	r = pclose(pipefp);

	sigprocmask(SIG_SETMASK, &osig, NULL);
	if (r != 0) {
		g_warning(_("external command `%s' failed with code `%i'\n"), command, r);
		return -1;
	}

	return 0;
}

#define EXIT_IF_CANCELLED() \
{ \
	GTK_EVENTS_FLUSH(); \
	if (dialog->cancelled) { \
		session_destroy(session); \
		send_progress_dialog_destroy(dialog); \
		return -1; \
	} \
}

#define SEND_EXIT_IF_ERROR(f, s) \
{ \
	EXIT_IF_CANCELLED(); \
	if (!(f)) { \
		log_warning("Error occurred while %s\n", s); \
		session_destroy(session); \
		send_progress_dialog_destroy(dialog); \
		return -1; \
	} \
}

#define SEND_EXIT_IF_NOTOK(f, s) \
{ \
	gint ok; \
 \
	EXIT_IF_CANCELLED(); \
	if ((ok = (f)) != SM_OK) { \
		log_warning("Error occurred while %s\n", s); \
		if (ok == SM_AUTHFAIL) { \
			log_warning(_("SMTP AUTH failed\n")); \
			if (ac_prefs->tmp_pass) { \
				g_free(ac_prefs->tmp_pass); \
				ac_prefs->tmp_pass = NULL; \
			} \
			if (ac_prefs->tmp_smtp_pass) { \
				g_free(ac_prefs->tmp_smtp_pass); \
				ac_prefs->tmp_smtp_pass = NULL; \
			} \
		} \
		if (session->sock && \
		    smtp_quit(SMTP_SESSION(session)) != SM_OK) \
			log_warning(_("Error occurred while sending QUIT\n")); \
		session_destroy(session); \
		send_progress_dialog_destroy(dialog); \
		return -1; \
	} \
}

gint send_message_smtp(PrefsAccount *ac_prefs, GSList *to_list,
			      FILE *fp)
{
	SendProgressDialog *dialog;
	Session *session = NULL;
	GtkCList *clist;
	const gchar *text[3];
	gchar buf[BUFFSIZE];
	gushort port;
	gchar *domain;
	gchar *user = NULL;
	gchar *pass = NULL;
	GSList *cur;
	gint size;

	g_return_val_if_fail(ac_prefs != NULL, -1);
	g_return_val_if_fail(ac_prefs->address != NULL, -1);
	g_return_val_if_fail(ac_prefs->smtp_server != NULL, -1);
	g_return_val_if_fail(to_list != NULL, -1);
	g_return_val_if_fail(fp != NULL, -1);

	size = get_left_file_size(fp);
	if (size < 0) return -1;

#if USE_SSL
	port = ac_prefs->set_smtpport ? ac_prefs->smtpport :
		ac_prefs->ssl_smtp == SSL_TUNNEL ? SSMTP_PORT : SMTP_PORT;
#else
	port = ac_prefs->set_smtpport ? ac_prefs->smtpport : SMTP_PORT;
#endif
	domain = ac_prefs->set_domain ? ac_prefs->domain : NULL;

	if (ac_prefs->use_smtp_auth) {
		if (ac_prefs->smtp_userid) {
			user = ac_prefs->smtp_userid;
			if (ac_prefs->smtp_passwd)
				pass = ac_prefs->smtp_passwd;
			else if (ac_prefs->tmp_smtp_pass)
				pass = ac_prefs->tmp_smtp_pass;
			else {
				pass = input_dialog_query_password
					(ac_prefs->smtp_server, user);
				if (!pass) pass = g_strdup("");
				ac_prefs->tmp_smtp_pass = pass;
			}
		} else {
			user = ac_prefs->userid;
			if (ac_prefs->passwd)
				pass = ac_prefs->passwd;
			else if (ac_prefs->tmp_pass)
				pass = ac_prefs->tmp_pass;
			else {
				pass = input_dialog_query_password
					(ac_prefs->smtp_server, user);
				if (!pass) pass = g_strdup("");
				ac_prefs->tmp_pass = pass;
			}
		}
	}

	dialog = send_progress_dialog_create();

	text[0] = NULL;
	text[1] = ac_prefs->smtp_server;
	text[2] = _("Standby");
	clist = GTK_CLIST(dialog->dialog->clist);
	gtk_clist_append(clist, (gchar **)text);

	if (ac_prefs->pop_before_smtp
	    && (ac_prefs->protocol == A_APOP || ac_prefs->protocol == A_POP3)
	    && (time(NULL) - ac_prefs->last_pop_login_time) > (60 * ac_prefs->pop_before_smtp_timeout)) {
		g_snprintf(buf, sizeof(buf), _("Doing POP before SMTP..."));
		statusbar_puts_all(buf);
		progress_dialog_set_label(dialog->dialog, buf);
		gtk_clist_set_text(clist, 0, 2, _("POP before SMTP"));
		GTK_EVENTS_FLUSH();
		inc_pop_before_smtp(ac_prefs);
	}
	
	session = smtp_session_new();
	session->data = dialog;
	session->ui_func = (SessionUIFunc)send_progress_dialog_update;

#if USE_SSL
	SEND_EXIT_IF_NOTOK
		(smtp_connect(SMTP_SESSION(session), ac_prefs->smtp_server,
			      port, domain, user, pass, ac_prefs->ssl_smtp),
		 "connecting to server");
#else
	SEND_EXIT_IF_NOTOK
		(smtp_connect(SMTP_SESSION(session), ac_prefs->smtp_server,
			      port, domain, user, pass),
		 "connecting to server");
#endif

	if (user) {
		SEND_EXIT_IF_NOTOK(smtp_auth(SMTP_SESSION(session),
					     ac_prefs->smtp_auth_type),
				   "authenticating");
	}

	SEND_EXIT_IF_NOTOK
		(smtp_from(SMTP_SESSION(session), ac_prefs->address),
		 "sending MAIL FROM");

	for (cur = to_list; cur != NULL; cur = cur->next)
		SEND_EXIT_IF_NOTOK(smtp_rcpt(SMTP_SESSION(session),
				   (gchar *)cur->data),
				   "sending RCPT TO");

	SEND_EXIT_IF_NOTOK(smtp_data(SMTP_SESSION(session)), "sending DATA");

	/* send main part */
	SEND_EXIT_IF_ERROR
		(send_message_data(dialog, session->sock, fp, size) == 0,
		 "sending data");

	progress_dialog_set_label(dialog->dialog, _("Quitting..."));
	statusbar_puts_all(_("Quitting..."));
	GTK_EVENTS_FLUSH();

	SEND_EXIT_IF_NOTOK(smtp_eom(SMTP_SESSION(session)), "terminating data");
	SEND_EXIT_IF_NOTOK(smtp_quit(SMTP_SESSION(session)), "sending QUIT");

	statusbar_pop_all();

	session_destroy(session);
	send_progress_dialog_destroy(dialog);

	return 0;
}

#undef EXIT_IF_CANCELLED
#undef SEND_EXIT_IF_ERROR
#undef SEND_EXIT_IF_NOTOK

#define EXIT_IF_CANCELLED() \
{ \
	if (dialog->cancelled) return -1; \
}

#define SEND_EXIT_IF_ERROR(f) \
{ \
	EXIT_IF_CANCELLED(); \
	if ((f) <= 0) return -1; \
}

#define SEND_DIALOG_UPDATE() \
{ \
	gettimeofday(&tv_cur, NULL); \
	if (tv_cur.tv_sec - tv_prev.tv_sec > 0 || \
	    tv_cur.tv_usec - tv_prev.tv_usec > UI_REFRESH_INTERVAL) { \
		g_snprintf(str, sizeof(str), \
			   _("Sending message (%d / %d bytes)"), \
			   bytes, size); \
		progress_dialog_set_label(dialog->dialog, str); \
		statusbar_puts_all(str); \
		progress_dialog_set_percentage \
			(dialog->dialog, (gfloat)bytes / (gfloat)size); \
		GTK_EVENTS_FLUSH(); \
		gettimeofday(&tv_prev, NULL); \
	} \
}

static gint send_message_data(SendProgressDialog *dialog, SockInfo *sock,
			      FILE *fp, gint size)
{
	gchar buf[BUFFSIZE];
	gchar str[BUFFSIZE];
	gint bytes = 0;
	struct timeval tv_prev, tv_cur;

	gettimeofday(&tv_prev, NULL);

	/* output header part */
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		bytes += strlen(buf);
		strretchomp(buf);

		SEND_DIALOG_UPDATE();

		if (!g_strncasecmp(buf, "Bcc:", 4)) {
			gint next;

			for (;;) {
				next = fgetc(fp);
				if (next == EOF)
					break;
				else if (next != ' ' && next != '\t') {
					ungetc(next, fp);
					break;
				}
				if (fgets(buf, sizeof(buf), fp) == NULL)
					break;
				else
					bytes += strlen(buf);
			}
		} else {
			SEND_EXIT_IF_ERROR(sock_puts(sock, buf));
			if (buf[0] == '\0')
				break;
		}
	}

	/* output body part */
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		bytes += strlen(buf);
		strretchomp(buf);

		SEND_DIALOG_UPDATE();

		/* escape when a dot appears on the top */
		if (buf[0] == '.')
			SEND_EXIT_IF_ERROR(sock_write(sock, ".", 1));

		SEND_EXIT_IF_ERROR(sock_puts(sock, buf));
	}

	g_snprintf(str, sizeof(str), _("Sending message (%d / %d bytes)"),
		   bytes, size);
	progress_dialog_set_label(dialog->dialog, str);
	progress_dialog_set_percentage
		(dialog->dialog, (gfloat)bytes / (gfloat)size);
	GTK_EVENTS_FLUSH();

	return 0;
}

#undef EXIT_IF_CANCELLED
#undef SEND_EXIT_IF_ERROR
#undef SEND_DIALOG_UPDATE

static SendProgressDialog *send_progress_dialog_create(void)
{
	SendProgressDialog *dialog;
	ProgressDialog *progress;

	dialog = g_new0(SendProgressDialog, 1);

	progress = progress_dialog_create();
	gtk_window_set_title(GTK_WINDOW(progress->window),
			     _("Sending message"));
	gtk_signal_connect(GTK_OBJECT(progress->cancel_btn), "clicked",
			   GTK_SIGNAL_FUNC(send_cancel), dialog);
	gtk_signal_connect(GTK_OBJECT(progress->window), "delete_event",
			   GTK_SIGNAL_FUNC(gtk_true), NULL);
	gtk_window_set_modal(GTK_WINDOW(progress->window), TRUE);
	manage_window_set_transient(GTK_WINDOW(progress->window));

	progress_dialog_set_value(progress, 0.0);

	if (prefs_common.send_dialog_mode == SEND_DIALOG_ALWAYS) {
		gtk_widget_show_now(progress->window);
	}
	
	dialog->dialog = progress;
	dialog->queue_list = NULL;
	dialog->cancelled = FALSE;

	return dialog;
}

static void send_progress_dialog_destroy(SendProgressDialog *dialog)
{
	g_return_if_fail(dialog != NULL);
	if (prefs_common.send_dialog_mode == SEND_DIALOG_ALWAYS) {
		progress_dialog_destroy(dialog->dialog);
	}
	g_free(dialog);
}

static void send_cancel(GtkWidget *widget, gpointer data)
{
	SendProgressDialog *dialog = data;

	dialog->cancelled = TRUE;
}

static void send_progress_dialog_update(Session *session, SMTPPhase phase)
{
	gchar buf[BUFFSIZE];
	gchar *state_str = NULL;
	SendProgressDialog *dialog = (SendProgressDialog *)session->data;

	switch (phase) {
	case SMTP_CONNECT:
		g_snprintf(buf, sizeof(buf),
			   _("Connecting to SMTP server: %s ..."),
			   session->server);
		state_str = _("Connecting");
		log_message("%s\n", buf);
		break;
	case SMTP_HELO:
		g_snprintf(buf, sizeof(buf), _("Sending HELO..."));
		state_str = _("Authenticating");
		break;
	case SMTP_EHLO:
		g_snprintf(buf, sizeof(buf), _("Sending EHLO..."));
		state_str = _("Authenticating");
		break;
	case SMTP_AUTH:
		g_snprintf(buf, sizeof(buf), _("Authenticating..."));
		state_str = _("Authenticating");
		break;
	case SMTP_FROM:
		g_snprintf(buf, sizeof(buf), _("Sending MAIL FROM..."));
		state_str = _("Sending");
		break;
	case SMTP_RCPT:
		g_snprintf(buf, sizeof(buf), _("Sending RCPT TO..."));
		state_str = _("Sending");
		break;
	case SMTP_DATA:
	case SMTP_EOM:
		g_snprintf(buf, sizeof(buf), _("Sending DATA..."));
		state_str = _("Sending");
		break;
	case SMTP_QUIT:
		g_snprintf(buf, sizeof(buf), _("Quitting..."));
		state_str = _("Quitting");
		break;
	default:
		return;
	}

	progress_dialog_set_label(dialog->dialog, buf);
	gtk_clist_set_text(GTK_CLIST(dialog->dialog->clist), 0, 2, state_str);
	GTK_EVENTS_FLUSH();
}
