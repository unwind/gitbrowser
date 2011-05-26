
Gitbrowser
==========
Gitbrowser is a plugin for the [Geany](http://geany.org/) lightweight IDE. It implements a simple, read-only, static tree view that shows a collection of [Git](http://git-scm.com/) repositories.

##Adding Repositories##
To add a new repository to be browsed, right-click on Repositories and pick either "Add ..." or "Add from Document"

The "Add ..." command will open up a standard file chooser, and you can pick the root directory containing your repository (the directory that contains the .git directory). "Add from Document", will instead assume that the current document is part of a repository, and add that. It will find the root of the repository regardless of where in the repo the current document is located, it doesn't have to be in the repository's root.

##Quick Open##
An important part of Gitbrowser is the 'Quick Open' command. It's available by right-clicking either the top Repositories tree node, or on a specific repository's root node. If you open it through Repositories, it will inspect the current document to figure out which repository to do Quick Open in.

Once opened, Quick Open lets you use the arrow keys to navigate the big list of files contained in the repository, or type to filter (in "real-time") the list of files. This allows very quick navigation to a file once you know a few characters of its filename. The more uniqe the characters, the faster it will be, of course.

By default, Quick Open is bound to the keyboard shortcut Shift+Alt+O.

#Feedback#
Please contact the author, Emil Brink (by e-mailing &lt;emil@obsession.se&gt;) regarding any bugs, comments, or thoughts about Gitbrowser. Enjoy.
