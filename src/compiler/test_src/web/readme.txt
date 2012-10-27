install software Linux
* apt-get update
* sudo apt-get install libapache2-mod-fastcgi
* suco apt-get install apache2-MPM-worker
* sudo apt-get install libfcgi-dev

configure apache:
* modify /etc/apache2/mods-enabled/fastcgi.conf

<IfModule mod_fastcgi.c>
  AddHandler fastcgi-script .fcgi
  FastCgiIpcDir /var/lib/apache2/fastcgi
  # objeck web executable
  FastCgiServer /home/randy/Documents/Code/src/vm/obr_fcgi -processes 5
  # objeck controller alias
  ScriptAlias /c /home/randy/Documents/Code/src/vm/obr_fcgi
</IfModule>


start apache and monitor log:
* sudo /etc/init.d/apache2 restart
* tail -f /var/log/apache2/error.log
