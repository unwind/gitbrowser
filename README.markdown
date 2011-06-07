
Gitbrowser
===========
Gitbrowser (or "Git Browser") is a plugin for the [Geany](http://geany.org/) lightweight IDE. It implements a simple, read-only, static tree view that shows a collection of [Git](http://git-scm.com/) repositories.

Gitbrowser does *not* aim to be a complete "solution" for working with Git repositories; all it does is import the list of files, present it visually in a tree, and allow you to quickly jump to a given file.

I wrote it simply to "scratch an itch", I want something like this when working on my projects, but being quite new to Git I prefer to use it manually at a "low level", directly from the command line. Having a the plugin speeds up actual software development for me, since I can quickly jump between files and do edits.

##Installation##
At the moment, Gitbrowser is not available in any pre-packaged form, so you need to build it yourself. Luckily, that isn't too hard. The following steps should do it:

1. Grab the code
2. Using a terminal, enter the top-level `gitbrowser/` directory
3. Type `make`
4. Type `sudo ln -s $(pwd)/src/gitbrowser.so $(dirname $(dirname $(which geany)))/lib/geany`

The final step is the most complicated; the purpose is to create a symbolic link from Geany's system-wide plugin directory to the library you just built. This assumes that you will want to keep the plugin in the source directory; if you rather would install it permanently you can move it to the indicated location instead, and then delete the source code.

Keeping a symbolic link will of course make it easier to update the plugin if there are any changes (or if you do changes yourself).


##The Browser###
Once activated in Geany's Plugin Manager, Gitbrowser will add its own page to the sidebar notebook. Initially, it will be empty and look like this:
![Empty Browser](https://github.com/unwind/gitbrowser/raw/master/doc/screenshots/empty.png "Empty Browser")

Right-clicking on the word "Repositories" will bring up a popup menu that looks like so:
![Root Menu](doc/screenshots/root-menu.png "Root Menu")

Before you can do much here, you need to add one or more repositories to the browser.


##Adding Repositories##
To add a new repository to the browser pick either "Add ..." or "Add from Document" from the menu shown above.

The "Add ..." command will open up a standard file chooser, and you can pick the root directory containing your repository (the directory that contains the `.git/` directory). "Add from Document", will instead assume that the current document is part of a repository, and add that. It will find the root of the repository regardless of where in the repo the current document is located, it doesn't have to be in the repository's root.

The Gitbrowser's tree view will include *all* files that are part of a repository, regardless of type or extension. This is perhaps slighly pointless (you currently can't do anything with files that Geany can't open for editing), but it's also simple and does a lot to reinforce the idea that Gitbrowser simply lets you visualize your repositories as trees.


##Repository Commands##
Once you have one or more repositories added, you can right-click on each repository to get another popup menu:
![Repository Menu](doc/screenshots/repo-menu.png "Repository Menu")

The following sections describe the commands available on a repository.

###Quick Open###
An important part of Gitbrowser is the 'Quick Open' command. It's available by right-clicking either the top Repositories tree node, or on a specific repository's root node. If you open it through Repositories, it will inspect the current document to figure out which repository to do Quick Open in.

The Quick Open dialog looks like this:
![Quick Open screenshot](doc/screenshots/quickopen.png "Quick Open")

Once opened, Quick Open lets you use the arrow keys to navigate the big list of files contained in the repository, or type to filter (in "real-time") the list of files. This allows very quick navigation to a file once you know a few characters of its filename. Since many source code projects use regular naming schemes for its files, you can often cut down the number of files visible very quickly, and thus "home in" on the file you are interested in.

The label at the bottom shows how many files are displayed, and if filtering is active it also shows how many files have been hidden by it. You can select multiple files in the list, Gitbrowser will open them all.

By default, Quick Open is bound to the keyboard shortcut Shift+Alt+O.


###Refreshing Repositories###
Gitbrowser will not automatically detect if files in a repository are added or removed. So to re-synchronize the browser you can use the Refresh command from the repository menu.


###Reordering Repositories###
Every time you add a repository, it will be appended to the end of the list of repositories. To change the order, right-click on the repository you would like to move and choose either "Move Up" or "Move Down".


###Removing a Repository###
Pick the Remove command from the repository right-click menu to remove that repository from the browser. This does not do anything to the files on disk, all it does is remove the repository from the list maintained by the Gitbrowser plugin.


##The Contents of a Repository##
Gitbrowser does not implement a whole lot of functionality for working with the actual contents of the repositories that you have added.

If you right-click on a directory inside a repository, you will see this menu:
![Directory Menu](doc/screenshots/dir-menu.png "Directory Menu")

These two obviously-named commands simply let you expand or collapse the directory. Currently, Gitbrowser does not save the expand/collapse state of the included repositories' files.

Right-clicking on an actual file gives you this even more minimalistic menu:
![File Menu](doc/screenshots/file-menu.png "File Menu")

All you can do is make Gitbrowser attempt to open the file, which will simply open it in Geany.


##Configuring Gitbrowser##
You can access Gitbrowser's configuration through Geany. The window looks like this:
![Configuration Window](doc/screenshots/config.png "Configuration Window")

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
</dl>

Gitbrowser will save your configured settings, as well as the (properly ordered) list of added repositories, and remember them until the next time you run Geany. The configuration is typically stored in a plain text file called `$(HOME)/.config/geany/plugins/gitbrowser/gitbrowser.conf`, where `$(HOME)` refers to your home directory.

#Feedback#
Please contact the author, Emil Brink (by e-mailing &lt;emil@obsession.se&gt;) regarding any bugs, comments, or thoughts about Gitbrowser. Enjoy.
