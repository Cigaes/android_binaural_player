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
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.Reader;
import java.util.ArrayList;

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
import android.view.ViewGroup;
import android.widget.*;

public class Binaural_player_GUI extends TabActivity
    implements View.OnClickListener, CompoundButton.OnCheckedChangeListener,
    Browser.File_click_listener, ServiceConnection, Handler.Callback
{
    /*
     * Global status
     */

    SharedPreferences global_settings;
    int play_total_time;
    String play_total_time_s;
    boolean play_paused = false;

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

    File tab_seq_file_path;
    TextView tab_seq_file_name;
    TextView tab_seq_dir_name;
    TextView tab_seq_description;
    Button tab_seq_button_play;

    EditText tab_edit_duration;
    EditText tab_edit_pink;
    EditText[] tab_edit_tones;
    Button tab_edit_button_edit;
    Button tab_edit_button_play;

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
	ifl.inflate(R.layout.tab_edit, tab_host.getTabContentView(), true);
	ifl.inflate(R.layout.tab_play, tab_host.getTabContentView(), true);
	tab_seq_file_name = (TextView)findViewById(R.id.tab_seq_file_name);
	tab_seq_dir_name = (TextView)findViewById(R.id.tab_seq_dir_name);
	tab_seq_description = (TextView)findViewById(R.id.tab_seq_description);
	tab_seq_button_play = (Button)findViewById(R.id.tab_seq_play);
	tab_seq_button_play.setOnClickListener(this);
	tab_edit_duration = (EditText)findViewById(R.id.tab_edit_duration);
	tab_edit_pink = (EditText)findViewById(R.id.tab_edit_pink);
	int[] te_id = { R.id.tab_edit_f1_carrier, R.id.tab_edit_f1_beat,
	    R.id.tab_edit_f1_vol, R.id.tab_edit_f2_carrier,
		R.id.tab_edit_f2_beat, R.id.tab_edit_f2_vol };
	tab_edit_tones = new EditText[te_id.length];
	for(int i = 0; i < te_id.length; i++)
	    tab_edit_tones[i] = (EditText)findViewById(te_id[i]);
	tab_edit_button_edit = (Button)findViewById(R.id.tab_edit_edit);
	tab_edit_button_edit.setOnClickListener(this);
	tab_edit_button_play = (Button)findViewById(R.id.tab_edit_play);
	tab_edit_button_play.setOnClickListener(this);
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
	browser.set_glob(".*\\.sbg.*");

	shell_script_view = new LinearLayout(this);
	shell_script_view.setOrientation(LinearLayout.VERTICAL);

	tab_host.addTab(tab_host.newTabSpec("Browser")
		.setIndicator("Browser")
		.setContent(browser));
	tab_host.addTab(tab_host.newTabSpec("Sequence")
		.setIndicator("Sequence")
		.setContent(R.id.tab_sequence));
	tab_host.addTab(tab_host.newTabSpec("Edit")
		.setIndicator("Edit")
		.setContent(R.id.tab_edit));
	tab_host.addTab(tab_host.newTabSpec("Play")
		.setIndicator("Play")
		.setContent(R.id.tab_play));
	tab_host.addTab(tab_host.newTabSpec("Options")
		.setIndicator("Options")
		.setContent(new Tab_content_wrapper(shell_script_view)));
	tab_host.setCurrentTabByTag("Browser");
	shell_script_tab_view = tab_host.getTabWidget().getChildTabViewAt(4);
	shell_script_tab_close();
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
	    player_service_disconnect();
	    System.exit(0);
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
	    on_click_seq_play_button();
	} else if(v == tab_edit_button_edit) {
	    on_click_edit_edit_button();
	} else if(v == tab_edit_button_play) {
	    on_click_edit_play_button();
	} else if(v == tab_play_button_pause) {
	    on_click_pause_button();
	} else if(v == tab_play_button_stop) {
	    on_click_stop_button();
	} else if(v == shell_script_button_play) {
	    on_click_shell_script_play();
	} else if(v == shell_script_button_close) {
	    on_click_shell_script_close();
	} else {
	    warn("Unknown button.");
	}
    }

    void on_click_seq_play_button()
    {
	String sequence = tab_seq_description.getText().toString();
	if (tab_seq_file_path != null &&
	    tab_seq_file_path.getName().endsWith(".sbgx") &&
	    sequence.startsWith("#!/bin/sh\n")) {
	    shell_script_play(tab_seq_file_path, null);
	} else {
	    play_sequence(sequence);
	}
    }

    void on_click_edit_edit_button()
    {
	String seq = edit_generate();
	if(seq != null)
	    sequence_set(seq);
    }

    void on_click_edit_play_button()
    {
	String seq = edit_generate();
	if(seq != null)
	    play_sequence(seq);
    }

    void play_sequence(String sequence)
    {
	if(sequence == null || sequence.indexOf(':') < 0)
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

    String edit_generate()
    {
	try {
	    int dur = Integer.parseInt(tab_edit_duration.getText().toString());
	    if(dur < 1)
		return null;
	    String pink = tab_edit_pink.getText().toString();
	    String decl = "beat:";
	    if(!pink.equals("0"))
		decl += " pink/" + pink;
	    for(int i = 0; i < tab_edit_tones.length; i += 3) {
		String carrier = tab_edit_tones[i].getText().toString();
		String beat = tab_edit_tones[i + 1].getText().toString();
		String vol = tab_edit_tones[i + 2].getText().toString();
		if(!beat.startsWith("-"))
		    beat = "+" + beat;
		if(!vol.equals("0"))
		    decl += " " + carrier + beat + "/" + vol;
	    }
	    String end = String.format("%02d:%02d:00", dur / 60, dur % 60);
	    String seq = String.format("%s\noff: -\nNOW beat\n+%s off\n",
		decl, end);
	    return seq;
	} catch(Exception e) {
	    error_dialog_show(e.toString());
	    return null;
	}
    }

    void sequence_set(String sequence)
    {
	tab_seq_description.setText(sequence);
	tab_host.setCurrentTabByTag("Sequence");
    }

    void sequence_info_load(File file)
    {
	tab_seq_file_path = file;
	tab_seq_file_name.setText(file.getName());
	tab_seq_dir_name.setText(file.getParent());
	String sequence;
	try {
	    sequence = read_file(file);
	} catch (java.io.IOException e) {
	    error_dialog_show("Error reading file:\n" + e);
	    return;
	}
	sequence_set(sequence);
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
	tab_play_button_pause.setText(pause ? "▶" : "❚❚");
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

    /*
     * Shell script
     */

    LinearLayout shell_script_view;
    View shell_script_tab_view;
    Button shell_script_button_close;
    Button shell_script_button_play;
    ToggleButton[][] shell_script_buttons;
    File shell_script_file;
    String[][] shell_script_options_vals;
    String[] shell_script_options_val;

    void shell_script_play(File file, String[] opts)
    {
	String filtered = null;
	try {
	    int nopts = opts == null ? 0 : opts.length;
	    String cmd[] = new String[2 + nopts];
	    cmd[0] = "/system/bin/sh";
	    cmd[1] = file.getPath();
	    for (int i = 0; i < nopts; i++)
		cmd[i + 2] = opts[i];
	    Process process = Runtime.getRuntime().exec(cmd);
	    Reader r = new InputStreamReader(process.getInputStream());
	    filtered = read_from_stream(r);
	} catch (java.io.IOException e) {
	    error_dialog_show("Error reading from script:\n" + e);
	    return;
	}

	if (filtered.startsWith("sbg_script_options\n")) {
	    String[] lines = filtered.substring(19).split("\n");
	    shell_script_configure(file, lines);
	} else if (filtered.startsWith("sbg_script -SE\n")) {
	    sequence_set(filtered.substring(11));
	} else {
	    int l = filtered.length();
	    error_dialog_show("Invalid script output:\n" +
		(l > 400 ? filtered.substring(0, 400) : filtered));
	}
    }

    void shell_script_configure(File file, String[] lines)
    {
	shell_script_view.removeAllViews();
	LinearLayout subview = new LinearLayout(this);
	subview.setOrientation(LinearLayout.VERTICAL);
	LinearLayout.LayoutParams lparams = new LinearLayout.LayoutParams(
	    ViewGroup.LayoutParams.WRAP_CONTENT,
	    ViewGroup.LayoutParams.WRAP_CONTENT);
	lparams.weight = 1;
	subview.setLayoutParams(lparams);
	shell_script_view.addView(subview);

	shell_script_buttons = new ToggleButton[lines.length][];
	shell_script_options_vals = new String[lines.length][];
	shell_script_options_val = new String[lines.length];
	for (int i = 0; i < lines.length; i++) {
	    String line = lines[i];
	    if (!line.startsWith("option "))
		continue;
	    String[] val = line.substring(7).split(" ");
	    if (val.length < 2)
		continue;

	    TextView label = new TextView(this);
	    label.setText(val[0]);
	    subview.addView(label);

	    int nb_vals = val.length - 1;
	    LinearLayout row = null;
	    shell_script_buttons[i] = new ToggleButton[nb_vals];
	    shell_script_options_vals[i] = new String[nb_vals];
	    ToggleButton def = null;
	    for (int j = 0; j < nb_vals; j++) {
		String text = val[j + 1];
		if (j % 5 == 0) {
		    row = new LinearLayout(this);
		    row.setOrientation(LinearLayout.HORIZONTAL);
		    subview.addView(row);
		}
		ToggleButton button = new ToggleButton(this);
		if (def == null)
		    def = button;
		if (text.startsWith("*")) {
		    def = button;
		    text = text.substring(1);
		}
		row.addView(button);
		button.setText(text);
		button.setTextOn(text);
		button.setTextOff(text);
		int[] tag = { i, j };
		button.setTag(tag);
		shell_script_buttons[i][j] = button;
		shell_script_options_vals[i][j] = text;
		button.setOnCheckedChangeListener(this);
	    }
	    def.setChecked(true);
	}

	LinearLayout button_row = new LinearLayout(this);
	button_row.setOrientation(LinearLayout.HORIZONTAL);
	shell_script_view.addView(button_row);
	shell_script_button_close = create_button(button_row, lparams, "×");
	shell_script_button_play  = create_button(button_row, lparams, "▶");

	shell_script_file = file;
	shell_script_tab_view.setVisibility(View.VISIBLE);
	tab_host.setCurrentTabByTag("Options");
    }

    public void onCheckedChanged(CompoundButton button, boolean checked)
    {
	if (!checked)
	    return;
	int[] tag = (int[])button.getTag();
	int optid = tag[0];
	int optval = tag[1];
	for (int i = 0; i < shell_script_buttons[optid].length; i++)
	    if (i != optval)
		shell_script_buttons[optid][i].setChecked(false);
	shell_script_options_val[optid] =
	    shell_script_options_vals[optid][optval];
    }

    void on_click_shell_script_play()
    {
	String[] optval = shell_script_options_val;
	File file = shell_script_file;
	shell_script_tab_close();
	shell_script_play(file, optval);
    }

    void on_click_shell_script_close()
    {
	shell_script_tab_close();
	tab_host.setCurrentTabByTag("Browser");
    }

    void shell_script_tab_close()
    {
	shell_script_view.removeAllViews();
	shell_script_tab_view.setVisibility(View.GONE);
	shell_script_file = null;
	shell_script_options_val = null;
	shell_script_options_vals = null;
    }

    /*
     * Mist interface
     */

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

    Button create_button(ViewGroup parent, ViewGroup.LayoutParams params,
	String text)
    {
	Button b = new Button(this);
	b.setText(text);
	b.setLayoutParams(params);
	b.setOnClickListener(this);
	parent.addView(b);
	return b;
    }

    /*
     * Utility functions
     */

    String read_from_stream(Reader reader)
	throws java.io.IOException
    {
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
    }

    String read_file(File file)
	throws java.io.IOException
    {
	return read_from_stream(new FileReader(file));
    }

    static void warn(String fmt, Object... args) {
	android.util.Log.v("Binaural_player_GUI", String.format(fmt, args));
    }
}

class Tab_content_wrapper implements TabHost.TabContentFactory {

    View view;

    Tab_content_wrapper(View v)
    {
	view = v;
    }

    public android.view.View createTabContent(String tag)
    {
	return view;
    }
}
