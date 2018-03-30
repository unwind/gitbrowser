Gitbrowser
===========
Gitbrowser (or "Git Browser") is a plugin for the [Geany](http://geany.org/) lightweight IDE. It implements a simple, read-only, static tree view that shows a collection of [Git](http://git-scm.com/) repositories and all their files.

Gitbrowser does *not* aim to be a complete "solution" for working with Git repositories; all it does is import the list of files, present it visually in a tree, and allow you to quickly jump to a given file.

Gitbrowser is based on the idea that since you already work with one system (Git) to define the set of files that is important for a particular project, it makes sense to be able to re-use *that set* in your editing environment. Maintaining a separate "project" file with more meta information seems to violate the [DRY](http://en.wikipedia.org/wiki/Don%27t_repeat_yourself) principle.

I wrote it simply to [scratch the proverbial itch](http://www.neilgunton.com/doc/open_source_myths#185364); I want something like this when working on my projects, but being quite new to Git I prefer to use it manually at a "low level", directly from the command line. This is why Gitbrowser doesn't implement any actual Git operations. Having the plugin speeds up actual software development for me, since I can quickly jump between files and do edits. 

## Installation ##
At the moment, Gitbrowser is not available in any pre-packaged form, so you need to build it yourself. Luckily, that isn't too hard. The following steps should do it:

1. Grab the code (`git clone git://github.com/unwind/gitbrowser.git`)
2. Using a terminal, enter the top-level `gitbrowser/` directory
3. Type `make`
4. There are two options here. If you just want to *use* the version of Gitbrowser you just built, and not keep updating it if the source changes, type `sudo make install`. If you want to keep the source (for updates, hacking, whatever), type `sudo make install-dev`.
5. If you went with the `make install` option, you can now delete the directory holding the source code.


## The Browser ##
Once activated in Geany's Plugin Manager, Gitbrowser will add its own page to the sidebar notebook. Initially, it will be empty and look like this:

![Empty Browser](https://github.com/unwind/gitbrowser/raw/master/doc/screenshots/empty.png "Empty Browser")

Right-clicking on the word "Repositories" will bring up a popup menu that looks like so:

![Root Menu](https://github.com/unwind/gitbrowser/raw/master/doc/screenshots/root-menu.png "Root Menu")

Before you can do much here, you need to add one or more repositories to the browser.


## Adding Repositories ##
To add a new repository to the browser pick either "Add ..." or "Add from Document" from the menu shown above.

The "Add ..." command will open up a standard file chooser, and you can pick the root directory containing your repository (the directory that contains the `.git/` directory). "Add from Document", will instead assume that the current document is part of a repository, and add that. It will find the root of the repository regardless of where in the repo the current document is located, it doesn't have to be in the repository's root.

When you add a repository, Gitbrowser will log a message to Geany's general Status window:

    22:48:21: Built repository "gitbrowser", 11 files added in 3.3 ms.
    22:48:22: Built repository "linux-2.6", 36707 files added in 458.2 ms.

As you can see, adding a repository is quite fast, even for very large repositories like the Linux kernel (your milage might vary, this is of course machine-dependent).

Gitbrowser's tree view will include *all* files that are part of each repository, regardless of type or extension. This is perhaps slighly pointless (you currently can't do anything with files that Geany can't open for editing), but it's also simple and does a lot to reinforce the idea that Gitbrowser simply lets you visualize your repositories as trees.

Note that Gitbrowser will indicate the branch of each repository by adding it enclosed in square brackets after the name of the repository.

### Adding Separators ###
Separators are simply thin horizontal lines that live in the list of repositories. They are completely passive, all they do is visually separate the repositories from each other.

They can allow you to establish some structure and order among your repos, if you have many. I use a separator between my personal repositories, and those that are forks of others' projects.


## Repository Commands ##
Once you have one or more repositories added, you can right-click on each repository to get another popup menu:

![Repository Menu](https://github.com/unwind/gitbrowser/raw/master/doc/screenshots/repo-menu.png "Repository Menu")

The following sections describe the commands available on a repository.

### Quick Open ###
An important part of Gitbrowser is the 'Quick Open' command. It's available by right-clicking either the top Repositories tree node, or on a specific repository's root node. If you open it through Repositories, it will inspect the current document to figure out which repository to do Quick Open in.

The Quick Open dialog looks like this:

![Quick Open screenshot](https://github.com/unwind/gitbrowser/raw/master/doc/screenshots/quickopen.png "Quick Open")

Once opened, Quick Open lets you use the arrow keys to navigate the big list of files contained in the repository, or type to filter (in "real-time") the list of files.
This allows very quick navigation to a file once you know a few characters of its filename.
Since many source code projects use regular naming schemes for its files, you can often cut down the number of files visible very quickly, and thus "home in" on the file you are interested in.

The list is sorted on the [Levenshtein distance](http://en.wikipedia.org/wiki/Levenshtein_distance) from the text typed in the filtering box.
This is an attempt to maximize the chance of the filtering helping to quickly bring the desired file into view.

Note that the filtering is done by literal sub-string, the text you type is not interpreted as a regular expression or any other form of abstract pattern. The filtering is, however, case-insensitive, so you can type just `make` to show all `Makefiles` in a project, for instance. This makes access as fast as possible, since typing lower-case characters is typically quicker.

The label at the bottom shows how many files are displayed, and if filtering is active it also shows how many files have been hidden by it. You can select multiple files in the list, Gitbrowser will open them all.

By default, Quick Open is bound to the keyboard shortcut <kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>O</kbd>.


### Greping a Repository ###
This is simply a GUI:fied way of running `git grep`, and collecting the output into Geany's message window.
Right-clicking the repository and selecting "Grep" or pressing the keyboard shortcut (<kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>G</kbd> by default) opens this dialog:

![Grep Dialog](https://raw.githubusercontent.com/unwind/gitbrowser/master/doc/screenshots/grep.png "Grep")

The text entry will be filled in with either the currently selected text, or the word under the cursor if no selection exists.

- If "Use regular expressions" is *not* selected, Gitbrowser passes the `--fixed-strings` option to "git grep".
- If "Case sensitive" is *not* selected, Gitbrowser passes the `--ignore-case` option to "git grep".
- Select "Invert" to search using the `--invert-match` option.
- Select "Match only a whole word" to pass the `--word-regexp` option.
- Select "Clear Messages" to clear Geany's message status window, before showing the results.

The matches returned by git will be added to Geany's message window, and the relevant tab will be displayed for easy access.
A count of matches is also shown.

When you click something that looks like a filename followed by a line number in the message window, Geany will attempt to open the file and put the cursor on the indicated line.


### Exploring ###
The "Explore ..." menu command will try to open the local directory using your system's default file browser.
For systems running e.g. the GNOME desktop, this will open a new Nautilus window showing the repository's root directory.


### Terminal ###
The "Terminal ..." menu command will open a new terminal (command-line) window in the repository's root directory.
You can configure which command Gitbrowser should run in order to open the terminal, in the Plugin preferences window.


### Refreshing Repositories ###
Gitbrowser will not automatically detect if files in a repository are added or removed. So to re-synchronize the browser you can use the Refresh command from the repository menu.


### Reordering Repositories ###
Every time you add a repository, it will be appended to the end of the list of repositories. To change the order, right-click on the repository you would like to move and choose either "Move Up" or "Move Down".


### Removing a Repository ###
Pick the Remove command from the repository right-click menu to remove that repository from the browser. This does not do anything to the files on disk, all it does is remove the repository from the list maintained by the Gitbrowser plugin.


## The Contents of a Repository ##
Gitbrowser does not implement a whole lot of functionality for working with the actual contents of the repositories that you have added.

If you right-click on a directory inside a repository, you will see this menu:

![Directory Menu](https://github.com/unwind/gitbrowser/raw/master/doc/screenshots/dir-menu.png "Directory Menu")

The first two obviously-named commands simply let you expand or collapse the directory.
Gitbrowser saves the expanded/collapsed state of the included repositories' directories when you quit Geany, and loads them back in when restarted.
The Explore and Terminal commands work just as for repositories (which makes sense, since repositories are directories too).

Right-clicking on an actual file gives you this, rather tiny, menu:

![File Menu](https://github.com/unwind/gitbrowser/raw/master/doc/screenshots/file-menu.png "File Menu")

The first choice is the most typical operation, which makes Gitbrowser attempt to open the file in Geany for editing.

The second choice lets you copy the full name, including the physical path on your local computer, of the right-clicked file to the system's clipboard.
This can be useful if you want to e.g. run shell commands involving this file, include it in documentation, and so on.


## Configuring Gitbrowser ##
You can access Gitbrowser's configuration through Geany. The window looks like this:

![Configuration Window](https://github.com/unwind/gitbrowser/raw/master/doc/screenshots/config.png "Configuration Window")

The options are as follows:

<dl>
<dt>Always hide files matching (RE)</dt>
<dd>Specify a <a href="http://developer.gnome.org/glib/stable/glib-regex-syntax.html">regular expression</a> that will be used to filter out files from the
Quick Open dialog. This is handy if your repository contains a lot of files (for example images, as in the pictured expression) that you never are going to
want to open using the Quick Open dialog. Filtering them out makes the list shorter, which makes opening and handling it faster.
</dd>

<dt>Keyboard filter for max (ms)</dt>
<dd>Specify a time, in milliseconds, that is the maximum amount of time Gitbrowser should spend updating the filter as you type. For very large repositories,
Gitbrowser currently won't be able to filter in real-time. So, to keep the interface from blocking totally, the filtering is "time-boxed" using this setting.
It will keep running every time GTK+ is idle, but never use more than the specified number of milliseconds before yielding control back to the application.
<p>
This means that even if the filtering operation as a whole requires many seconds (which is, unfortunately, not impossible for very large repositories), you will
not have to wait that long if you e.g. change your mind and want to cancel the Quick Open dialog.
</p>
</dd>

<dt>Terminal command</dt>
<dd>Specify the command that Gitbrower should run in order to open a terminal emulator window.
<p>
You can optionally start the command name with a dollar (<code>$</code>) symbol to make Gitbrowser treat the rest of the name as an environment variable.
The variable's current value will be looked-up, and if it succeeds the resulting string will be used as the command.
</p>
<p>
Note that you cannot specify any arguments to the terminal emulator; the entire string will be interpreted as the command name.
</p>
</dl>

Gitbrowser will save your configured settings, as well as the (properly ordered) list of added repositories, and remember them until the next time you run Geany. The configuration is typically stored in a plain text file called `$(HOME)/.config/geany/plugins/gitbrowser/gitbrowser.conf`, where `$(HOME)` refers to your home directory.

# Feedback #
Please contact the author, Emil Brink (by e-mailing &lt;emil@obsession.se&gt;) regarding any bugs, comments, or thoughts about Gitbrowser. Enjoy.
