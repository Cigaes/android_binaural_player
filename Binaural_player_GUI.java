/*
 * Binaural player
 * © Nicolas George -- 2010
 * Application graphical user interface
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

import java.io.File;
import java.io.FileReader;
import java.nio.CharBuffer;

import android.app.TabActivity;
import android.app.AlertDialog;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Handler;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;
import android.content.Context;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.graphics.drawable.Drawable;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.*;

public class Binaural_player_GUI extends TabActivity
    implements View.OnClickListener, Browser.File_click_listener,
    ServiceConnection, Handler.Callback
{
    /*
     * Global status
     */

    SharedPreferences global_settings;
    int play_total_time;
    String play_total_time_s;
    boolean play_paused = false;
    String sequence;

    @Override
    public void onCreate(Bundle state)
    {
	super.onCreate(state);
	global_settings = getPreferences(Context.MODE_WORLD_READABLE);
	user_interface_create();
	player_service_connect();
	String dir = state == null ? null : state.getString("dir");
	if(dir == null)
	    dir = global_settings.getString("default-dir", "/sdcard");
	browser.chdir(dir);
    }

    @Override
    public void onDestroy()
    {
	super.onDestroy();
	player_service_disconnect();
    }

    @Override
    public void onSaveInstanceState(Bundle state)
    {
	state.putString("dir", browser.get_dir().getPath());
    }

    /*
     * Communication with the player service.
     */

    Intent player_service_intent;
    Messenger player_service;
    boolean player_service_connecting = false;
    final Messenger incoming_messenger = new Messenger(new Handler(this));

    void player_service_connect()
    {
	player_service_intent = new Intent();
	player_service_intent.addCategory(Intent.CATEGORY_DEFAULT);
	player_service_intent.setAction(Intent.ACTION_RUN);
	player_service_intent.setType("application/x-sbagen-sequence");
	startService(player_service_intent);
	player_service_connecting = true;
	bindService(player_service_intent, this, Context.BIND_AUTO_CREATE);
    }

    void player_service_disconnect()
    {
	player_service_connecting = false;
	if(player_service == null)
	    return;
	Message msg = Message.obtain(null, 'J');
	player_service_send_message(msg);
	unbindService(this);
	player_service = null;
	player_service_intent = null;
    }

    public void onServiceConnected(ComponentName name, IBinder binder)
    {
	player_service = new Messenger(binder);
	if(player_service_connecting) {
	    Message msg = Message.obtain(null, 'I');
	    player_service_send_message(msg);
	} else {
	    player_service_disconnect();
	}
    }

    public void onServiceDisconnected(ComponentName name)
    {
	warn("service [%s] disconnected", name);
	player_service = null;
    }

    public void player_service_send_message(Message msg)
    {
	if(player_service == null) {
	    warn("player_service_send_message: not connected");
	    return;
	}
	msg.replyTo = incoming_messenger;
	try {
	    player_service.send(msg);
	} catch(RemoteException e) {
	    warn("exception: %s", e);
	}
    }

    public boolean handleMessage(Message msg)
    {
	Bundle b;
	switch(msg.what) {
	    case 'S':
		b = msg.getData();
		tab_play_set_sequence(b.getString("seq"),
		    b.getInt("duration"));
		return true;
	    case 'T':
		tab_play_set_time(msg.arg1);
		return true;
	    case 'P':
		tab_play_set_pause(msg.arg1 != 0);
		return true;
	    case 'E':
		b = msg.getData();
		error_dialog_show(b.getString("message"));
		return true;
	    default:
		warn("Unknown message: %s", msg);
		return false;
	}
    }

    /*
     * User interaction
     */

    TabHost tab_host;
    Browser browser;

    TextView tab_seq_file_name;
    TextView tab_seq_dir_name;
    TextView tab_seq_description;
    Button tab_seq_button_play;

    TextView tab_play_description;
    TextView tab_play_time;
    ProgressBar tab_play_progress;
    Button tab_play_button_pause;
    Button tab_play_button_stop;

    MenuItem menu_item_default_dir;
    MenuItem menu_item_about;
    MenuItem menu_item_exit;

    void user_interface_create()
    {
	tab_host = getTabHost();
	LayoutInflater ifl = LayoutInflater.from(this);
	ifl.inflate(R.layout.tab_sequence, tab_host.getTabContentView(), true);
	ifl.inflate(R.layout.tab_play, tab_host.getTabContentView(), true);
	tab_seq_file_name = (TextView)findViewById(R.id.tab_seq_file_name);
	tab_seq_dir_name = (TextView)findViewById(R.id.tab_seq_dir_name);
	tab_seq_description = (TextView)findViewById(R.id.tab_seq_description);
	tab_seq_button_play = (Button)findViewById(R.id.tab_seq_play);
	tab_seq_button_play.setOnClickListener(this);
	tab_play_description =
	    (TextView)findViewById(R.id.tab_play_description);
	tab_play_time = (TextView)findViewById(R.id.tab_play_time);
	tab_play_progress = (ProgressBar)findViewById(R.id.tab_play_progress);
	tab_play_button_pause = (Button)findViewById(R.id.tab_play_pause);
	tab_play_button_pause.setOnClickListener(this);
	tab_play_button_stop = (Button)findViewById(R.id.tab_play_stop);
	tab_play_button_stop.setOnClickListener(this);
	browser = new Browser(this);
	browser.set_file_click_listener(this);
	browser.set_glob(".*\\.sbg");
	tab_host.addTab(tab_host.newTabSpec("Browser")
		.setIndicator("Browser")
		.setContent(browser));
	tab_host.addTab(tab_host.newTabSpec("Sequence")
		.setIndicator("Sequence")
		.setContent(R.id.tab_sequence));
	tab_host.addTab(tab_host.newTabSpec("Play")
		.setIndicator("Play")
		.setContent(R.id.tab_play));
	tab_host.setCurrentTabByTag("Browser");
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu)
    {
	menu_item_default_dir = menu.add("Set default dir");
	menu_item_default_dir.setIcon(android.R.drawable.ic_menu_mylocation);
	menu_item_about = menu.add("About");
	menu_item_about.setIcon(android.R.drawable.ic_menu_info_details);
	menu_item_exit = menu.add("Exit");
	menu_item_exit.setIcon(android.R.drawable.ic_menu_close_clear_cancel);
	return true;
    }

    public boolean onOptionsItemSelected(MenuItem item)
    {
	if(item == menu_item_default_dir) {
	    SharedPreferences.Editor editor = global_settings.edit();
	    editor.putString("default-dir", browser.get_dir().getPath());
	    editor.commit();
	    Toast.makeText(this, "Default directory saved.", Toast.LENGTH_SHORT)
		.show();
	} else if(item == menu_item_about) {
	    about_dialog_show();
	} else if(item == menu_item_exit) {
	    on_click_stop_button();
	    finish();
	} else {
	    warn("Unknown menu item selected: %s", item.getTitle());
	}
	return true;
    }


    public void on_browser_file_click(File file)
    {
	sequence_info_load(file);
    }

    public void onClick(View v) {
	if(v == tab_seq_button_play) {
	    on_click_play_button();
	} else if(v == tab_play_button_pause) {
	    on_click_pause_button();
	} else if(v == tab_play_button_stop) {
	    on_click_stop_button();
	} else {
	    warn("Unknown button.");
	}
    }

    void on_click_play_button()
    {
	if(sequence == null)
	    return;
	Message msg = Message.obtain(null, 'R');
	Bundle b = new Bundle(1);
	b.putString("seq", sequence);
	msg.setData(b);
	player_service_send_message(msg);
    }

    void on_click_pause_button()
    {
	Message msg = Message.obtain(null, 'C');
	msg.arg1 = play_paused ? 'R' : 'P';
	player_service_send_message(msg);
    }

    void on_click_stop_button()
    {
	Message msg = Message.obtain(null, 'C');
	msg.arg1 = 'S';
	player_service_send_message(msg);
    }

    void sequence_info_load(File file)
    {
	tab_seq_file_name.setText(file.getName());
	tab_seq_dir_name.setText(file.getParent());
	sequence = read_file(file);
	tab_seq_description.setText(sequence);
	tab_seq_button_play.setClickable(sequence != null);
	tab_host.setCurrentTabByTag("Sequence");
    }

    void tab_play_set_sequence(String seq, int d)
    {
	tab_play_description.setText(seq);
	if(seq == null) {
	    tab_play_button_pause.setClickable(false);
	    tab_play_button_stop.setClickable(false);
	} else {
	    tab_host.setCurrentTabByTag("Play");
	    tab_play_button_pause.setClickable(true);
	    tab_play_button_stop.setClickable(true);
	    play_total_time = d;
	    play_total_time_s = d <= 0 ? "" : String.format(" / %d:%02d",
		(d + 500) / 60000, ((d + 500) / 1000) % 60);
	}
	tab_play_progress.setProgress(0);
	tab_play_time.setText(null);
	tab_play_set_pause(false);
    }

    void tab_play_set_pause(boolean pause)
    {
	play_paused = pause;
	int id = pause ?
	    android.R.drawable.ic_media_play :
	    android.R.drawable.ic_media_pause;
	Drawable d = getResources().getDrawable(id);
	tab_play_button_pause.setCompoundDrawablesWithIntrinsicBounds(
	    null, d, null, null);
    }

    void tab_play_set_time(int t)
    {
	String dt = String.format("%d:%02d", t / 60000, (t / 1000) % 60);
	if(play_total_time > 0) {
	    dt += play_total_time_s;
	    tab_play_progress.setProgress(100 * t / play_total_time);
	}
	tab_play_time.setText(dt);
    }

    void error_dialog_show(String text)
    {
	AlertDialog.Builder builder = new AlertDialog.Builder(this);
	builder.setMessage(text);
	builder.setNeutralButton("Ok", null);
	builder.setTitle("Error");
	builder.show();
    }

    void about_dialog_show()
    {
	AlertDialog.Builder builder = new AlertDialog.Builder(this);
	builder.setMessage("© Nicolas George — 2010\n" +
	    "(GNU GPLv2)\n" +
	    "nsup.org/~george/comp/binaural_player\n" +
	    "Based on SBaGen by Jim Peters");
	builder.setNeutralButton("Ok", null);
	builder.setTitle("Binaural player");
	builder.show();
    }

    /*
     * Utility functions
     */

    String read_file(File file)
    {
	try {
	    FileReader reader = new FileReader(file);
	    char[] b = new char[4096];
	    int bp = 0;
	    while(true) {
		int r = reader.read(b, bp, b.length - bp);
		if(r <= 0)
		    break;
		bp += r;
		if(bp == b.length) {
		    char[] nb = new char[b.length * 2];
		    for(int i = 0; i < b.length; i++)
			nb[i] = b[i];
		    b = nb;
		}
	    }
	    return new String(b, 0, bp);
	} catch(Exception e) {
	    return null;
	}
    }

    static void warn(String fmt, Object... args) {
	android.util.Log.v("Binaural_player_GUI", String.format(fmt, args));
    }
}
