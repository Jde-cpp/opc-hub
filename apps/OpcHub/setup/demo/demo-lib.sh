# What install-demo.sh and reset.sh both do to the nginx sites and the hub, sourced by each:  their own die() and set -euo pipefail.
#site <file> <link>:  /etc/jde-cpp/demo/<file> enabled as <link> if nginx takes it, else taken back out so the next reload of nginx
#still works.
site(){
	local out
	ln -sfn "/etc/jde-cpp/demo/$1" "/etc/nginx/sites-enabled/$2"
	if ! out=$(nginx -t 2>&1); then
		rm -f "/etc/nginx/sites-enabled/$2"
		die "nginx refused $1, which was not enabled:  $out"
	fi
	systemctl reload-or-restart nginx
}
#hubUp:  true once the hub answers on loopback, within a minute.  A unit that fails its start restarts every 5 s, and
#`systemctl start` says nothing of it.
hubUp(){
	for _ in $(seq 60); do
		curl -s -o /dev/null --max-time 5 http://127.0.0.1:1967/serverSettings && return 0
		sleep 1
	done
	return 1
}