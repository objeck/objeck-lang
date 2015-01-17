Setting up FCGI support for Objeck

== Install web server software on Linux ==
* apt-get update
* sudo apt-get install libapache2-mod-fastcgi
* suco apt-get install apache2-MPM-worker
* sudo apt-get install libfcgi-dev

== Configure Apache ==
modify /etc/apache2/mods-enabled/fastcgi.conf (see below)

<IfModule mod_fastcgi.c>
  AddHandler fastcgi-script .fcgi
  FastCgiIpcDir /var/lib/apache2/fastcgi
  FastCgiServer /home/randy/Documents/Code/src/vm/obr_fcgi -initial-env PROGRAM_PATH=../compiler/a.obw -processes 5
  ScriptAlias /fcgi /home/randy/Documents/Code/src/vm/obr_fcgi
</IfModule>

== Copy the supporting FCGI library ==
copy the fcgi.so file to the location specified in the vm/config.prop file.  For example:
* fcgi-lib-path=/var/lib/apache2/fastcgi/fcgi

== Start Apache ==
* sudo /etc/init.d/apache2 restart
* tail -f /var/log/apache2/error.log
