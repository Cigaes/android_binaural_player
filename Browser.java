/*
 * Binaural player
 * © Nicolas George -- 2010
 * Files browser
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
import java.util.Arrays;
import android.content.Context;
import android.widget.*;
import android.view.View;
import android.os.PatternMatcher;

public class Browser extends LinearLayout
    implements TabHost.TabContentFactory, AdapterView.OnItemClickListener
{
    File cur_dir;
    PatternMatcher glob;
    File_click_listener callback;
    final TextView wid_cur_dir;
    final ListView wid_files;
    final ArrayAdapter<String> files;

    public Browser(Context context)
    {
	super(context);
	setOrientation(VERTICAL);
	wid_cur_dir = new TextView(context);
	addView(wid_cur_dir);
	wid_files = new ListView(context);
	wid_files.setFastScrollEnabled(true);
	files = new ArrayAdapter<String>(context,
	    android.R.layout.simple_list_item_1);
	wid_files.setAdapter(files);
	wid_files.setOnItemClickListener(this);
	addView(wid_files);
    }

    public void set_file_click_listener(File_click_listener c)
    {
	callback = c;
    }

    public void set_glob(String g)
    {
	set_glob(new PatternMatcher(g, PatternMatcher.PATTERN_SIMPLE_GLOB));
    }

    public void set_glob(PatternMatcher m)
    {
	glob = m;
    }

    public void chdir(String dir)
    {
	chdir(new File(dir));
    }

    public void chdir(File dir)
    {
	cur_dir = dir;
	wid_cur_dir.setText(cur_dir.getPath());
	files.clear();
	files.add("../");
	File[] lf = dir.listFiles();
	if(lf == null)
	    return;
	Arrays.sort(lf);
	for(int i = 0; i < lf.length; i++) {
	    if(lf[i].isDirectory()) {
		files.add(lf[i].getName() + "/");
		lf[i] = null;
	    }
	}
	for(int i = 0; i < lf.length; i++) {
	    if(lf[i] == null)
		continue;
	    String n = lf[i].getName();
	    if(glob != null && !glob.match(n))
		continue;
	    files.add(n);
	    lf[i] = null;
	}
	wid_files.scrollTo(0, 0);
    }

    public File get_dir()
    {
	return cur_dir;
    }

    public void onItemClick(AdapterView list, View view, int item, long row)
    {
	String f = files.getItem(item);
	int fl = f.length() - 1;
	if(fl >= 0 && f.charAt(fl) == '/') {
	    String d = f.substring(0, fl);
	    File nd = d.equals("..") ? cur_dir.getParentFile() :
		new File(cur_dir, d);
	    //android.util.Log.v("Binaural_player", "-> " + );
	    chdir(nd);
	} else {
	    callback.on_browser_file_click(new File(cur_dir, f));
	}
    }

    public android.view.View createTabContent(String tag) {
	return this;
    }

    public interface File_click_listener {
	public void on_browser_file_click(File f);
    }
}
