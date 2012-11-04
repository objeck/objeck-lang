== Install web server software on Linux ==
* apt-get update
* sudo apt-get install libapache2-mod-fastcgi
* suco apt-get install apache2-MPM-worker
* sudo apt-get install libfcgi-dev

== Configure apache ==
* modify /etc/apache2/mods-enabled/fastcgi.conf

* Linux
<IfModule mod_fastcgi.c>
  AddHandler fastcgi-script .fcgi
  FastCgiIpcDir /var/lib/apache2/fastcgi
  FastCgiServer /home/randy/Documents/Code/src/vm/obr_fcgi -initial-env PROGRAM_PATH=../compiler/a.obw -processes 5
  ScriptAlias /fcgi /home/randy/Documents/Code/src/vm/obr_fcgi
</IfModule>

* Windows
copy the "mod_fastcgi-2.4.6-AP22.dll" file to the modules directory
copy the "libfcgi.dll" file to the "C:/Users/Alienware/Documents/Code/main/src/vm" directory
LoadModule fastcgi_module "modules/mod_fastcgi-2.4.6-AP22.dll"
<IfModule mod_fastcgi.c>
  <Directory "C:/Users/Alienware/Documents/Code/main/src/vm">
    Order allow,deny
	Allow from all
  </Directory>
  FastCgiServer "C:/Users/Alienware/Documents/Code/main/src/vm/vm_fcgi.exe" -idle-timeout 30
  ScriptAlias /fcgi "C:/Users/Alienware/Documents/Code/main/src/vm/vm_fcgi.exe"
</IfModule>

== Copy the supporting library ==
copy the fcgi.(so|dll|dylib) to the location specified in the vm/config.prop file.  For example /var/lib/apache2/fastcgi

start apache and monitor log:
* sudo /etc/init.d/apache2 restart
* tail -f /var/log/apache2/error.log
