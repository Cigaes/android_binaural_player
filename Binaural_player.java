/*
 * Binaural player
 * © Nicolas George -- 2010
 * Player service
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 */

package org.cigaes.binaural_player;

import java.util.ArrayList;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import android.app.Notification;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Handler;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;

public class Binaural_player extends Service implements Handler.Callback
{
    int playing_time = -1;
    boolean playing_paused = false;
    String playing_sequence;
    int playing_total_time;

    /*
     * Client interraction
     */

    final Messenger incoming_messenger = new Messenger(new Handler(this));
    final ArrayList<Messenger> clients = new ArrayList<Messenger>();

    @Override
    public IBinder onBind(Intent intent)
    {
	return incoming_messenger.getBinder();
    }

    @Override
    public boolean onUnbind(Intent intent)
    {
	return true;
    }

    public boolean handleMessage(Message msg)
    {
	Bundle b;
	switch(msg.what) {
	    case 'I':
		clients.add(msg.replyTo);
		client_send_status(msg.replyTo);
		client_send_time(msg.replyTo);
		client_send_pause(msg.replyTo);
		return true;
	    case 'J':
		clients.remove(msg.replyTo);
		exit_if_finished();
		return true;
	    case 'R':
		b = msg.getData();
		decoder_start(b.getString("seq"));
		return true;
	    case 'C':
		handle_client_control((char)msg.arg1);
		return true;
	    case 't':
		playing_paused = false;
		if(msg.arg1 >= 0) {
		    playing_time = msg.arg1;
		    client_send_time(null);
		} else {
		    decoder_reap();
		}
		return true;
	    case 'e':
		b = msg.getData();
		client_send_error(null, b.getString("message"));
		decoder_reap();
		return true;
	    default:
		warn("Unknown message: %s", msg);
		return false;
	}
    }

    void handle_client_control(char cmd)
    {
	switch(cmd) {
	    case 'S':
		decoder_stop();
		break;
	    case 'P':
		decoder_pause(true);
		break;
	    case 'R':
		decoder_pause(false);
		break;
	    default:
		warn("Unknown control: %c", cmd);
	}
    }

    void client_send_status(Messenger client)
    {
	Bundle b = new Bundle(2);
	b.putString("seq", playing_sequence);
	b.putInt("duration", playing_total_time);
	client_send_message(client, 'S', 0, b);
    }

    void client_send_time(Messenger client)
    {
	client_send_message(client, 'T', playing_time, null);
    }

    void client_send_pause(Messenger client)
    {
	client_send_message(client, 'P', playing_paused ? 1 : 0, null);
    }

    void client_send_error(Messenger client, String error)
    {
	Bundle b = new Bundle(1);
	b.putString("message", error);
	client_send_message(client, 'E', 0, b);
    }

    void client_send_message(Messenger client, char code, int arg1, Bundle b)
    {
	Message msg = Message.obtain(null, code);
	msg.arg1 = arg1;
	msg.setData(b);
	client_send_message(client, msg);
    }

    void client_send_message(Messenger client, Message msg)
    {
	if(client == null) {
	    for(Messenger c : clients)
		client_send_message(c, msg);
	    return;
	}
	try {
	    client.send(msg);
	} catch(RemoteException e) {
	    warn("client_send: exception: %s", e);
	    clients.remove(client);
	}
    }

    /*
     * Decoder thread interaction
     */

    Binaural_decoder decoder;
    Thread decoder_thread;
    String playing_next;

    void decoder_start(String seq)
    {
	if(decoder != null) {
	    decoder_stop();
	    playing_next = seq;
	    return;
	}
	playing_sequence = seq;
	playing_total_time = parse_total_time(seq);
	playing_time = 0;
	playing_paused = false;
	decoder = new Binaural_decoder(incoming_messenger, seq);
	decoder_thread = new Thread(decoder);
	decoder_thread.start();
	client_send_status(null);
	set_foreground();
    }

    void decoder_stop()
    {
	if(decoder == null)
	    return;
	decoder.set_command('S');
    }

    void decoder_pause(boolean pause)
    {
	if(decoder == null)
	    return;
	decoder.set_command(pause ? 'P' : 'R');
	playing_paused = pause;
	client_send_pause(null);
    }

    void decoder_reap()
    {
	playing_sequence = null;
	client_send_status(null);
	if(decoder == null)
	    return;
	stopForeground(true);
	while(true) {
	    try {
		decoder_thread.join();
		break;
	    } catch(InterruptedException e) {
	    }
	}
	decoder = null;
	decoder_thread = null;
	if(playing_next != null) {
	    String s = playing_next;
	    playing_next = null;
	    decoder_start(s);
	} else {
	    exit_if_finished();
	}
    }

    /*
     * System interaction
     */

    public void exit_if_finished()
    {
	if(clients.size() == 0 && decoder == null)
	    stopSelf();
    }

    void set_foreground()
    {
	Notification notification = new Notification(R.drawable.icon,
	    null, System.currentTimeMillis());
	Intent intent = new Intent();
	intent.addCategory(Intent.CATEGORY_DEFAULT);
	intent.setAction(Intent.ACTION_VIEW);
	intent.setType("application/x-sbagen-sequence");
	PendingIntent pintent = PendingIntent.getActivity(this, 0, intent, 0);
	notification.setLatestEventInfo(this, "Binaural player",
	    null, pintent);
	startForeground(1, notification);
    }

    /*
     * Utility functions
     */

    final Matcher sequence_time_parser =
	Pattern.compile("\\s*\\+(\\d+(?::\\d+)*)\\s+.*").matcher("");

    int parse_total_time(String seq)
    {
	String[] lines = seq.split("\n");
	for(int i = lines.length - 1; i >= 0; i--) {
	    if(lines[i].length() > 0) {
		sequence_time_parser.reset(lines[i]);
		if(sequence_time_parser.matches()) {
		    String[] c = sequence_time_parser.group(1).split(":");
		    int r = 0;
		    for(int j = 0; j < c.length; j++)
			r = r * 60 + Integer.parseInt(c[j]);
		    return r * 1000;
		}
		break;
	    }
	}
	return -1;
    }

    static void warn(String fmt, Object... args) {
	android.util.Log.v("Binaural_player", String.format(fmt, args));
    }
}
