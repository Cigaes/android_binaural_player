/*
 * Binaural player
 * © Nicolas George -- 2010
 * Decoder thread and wrapper around SBaGen
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

import android.os.Bundle;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;

import android.media.AudioTrack;
import android.media.AudioManager;
import android.media.AudioFormat;

class Binaural_decoder implements Runnable
{
    final int rate = 44100;

    final Messenger service;
    final String sequence;
    AudioTrack track;
    int play_pos = 0;
    int play_pos_notify = 0;
    char command = 0;

    Binaural_decoder(Messenger srv, String seq)
    {
	service = srv;
	sequence = seq;
    }

    public void run()
    {
	final int channels = AudioFormat.CHANNEL_OUT_STEREO;
	final int format = AudioFormat.ENCODING_PCM_16BIT;
	final int buffer_size =
	    AudioTrack.getMinBufferSize(rate, channels, format) * 10;
	track = new AudioTrack(AudioManager.STREAM_MUSIC,
	    rate, channels, format, buffer_size, AudioTrack.MODE_STREAM);
	short[] t = new short[100];
	try {
	    sbagen_init();
	    sbagen_set_parameters(rate, 0, 0, null);
	    sbagen_parse_seq(sequence);
	    track.play();
	    sbagen_run();
	    send_status(-1, null);
	} catch(InterruptedException e) {
	    send_status(-1, null);
	} catch(Exception e) {
	    send_status(-1, e.getMessage());
	}
	sbagen_free_seq();
	sbagen_exit();
    }

    void send_status(int t, String e)
    {
	Message msg = Message.obtain(null, 0);
	if(e == null) {
	    msg.what = 't';
	    msg.arg1 = t;
	} else {
	    msg.what = 'e';
	    Bundle b = new Bundle(2);
	    b.putString("message", e);
	    msg.setData(b);
	}
	try {
	    service.send(msg);
	} catch(RemoteException x) {
	}
    }

    synchronized void obey_command() throws InterruptedException
    {
	while(true) {
	    char cmd = command;
	    command = 0;
	    switch(cmd) {
		case 'S':
		    throw new InterruptedException();
		case 'P':
		    wait_new_command();
		    break;
		case 'R':
		    break;
		case 0:
		    return;
		default:
		    warn("obey_command: unknown command %c\n");
		    break;
	    }
	}
    }

    synchronized void wait_new_command()
    {
	track.pause();
	while(command == 0) {
	    try {
		wait();
	    } catch(InterruptedException e) {
	    }
	}
	track.play();
    }

    /* Called by sbagen_run. */
    public synchronized void out(short[] data) throws InterruptedException
    {
	obey_command();
	track.write(data, 0, data.length);
	play_pos += data.length / 2;
	if(play_pos >= play_pos_notify) {
	    send_status((int)(1000.0 * play_pos / rate), null);
	    play_pos_notify = play_pos + rate - 1;
	    play_pos_notify -= play_pos_notify % rate;
	}
    }

    public synchronized void set_command(char c)
    {
	command = c;
	notify();
    }

    static {
	System.loadLibrary("sbagen");
    }

    native void sbagen_init() throws OutOfMemoryError;
    native void sbagen_set_parameters(int rate, int prate, int fade,
	String roll) throws IllegalArgumentException;
    native void sbagen_exit();
    native void sbagen_parse_seq(String seq) throws IllegalArgumentException;
    native void sbagen_free_seq();
    native void sbagen_run() throws IllegalArgumentException,
       InterruptedException;

    static void warn(String fmt, Object... args) {
	android.util.Log.v("Binaural_player", String.format(fmt, args));
    }
}

