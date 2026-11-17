#!/usr/bin/env python3
# The public demo's seed, its half through the hub's GraphQL (docs/demo/plan.md Phase 8).  seed.sh runs it, as root:  it logs in
# to the loopback hub as the seed's own certificate user, `demo-seed`, with the key `seed.sh certs` left for root alone.  Every
# step looks its row up by slug first, so a second run changes nothing and a partial one is finished by the next.
#   seed.py access [--plcs <seconds>]   owner for the seed's user, the bundled server's and the flow server's connections purged, the guest, `visitor`, `demo-plc`,
#                                       every resource enforced;  --plcs waits that long for the five PLCs' first logins
#   seed.py grant <email>               owner for the Google user of that address
#   seed.py session                     a session of the seed's user on stdout, for smoke.sh, which ends it
import argparse, base64, json, os, subprocess, sys, time, urllib.error, urllib.request

HUB = os.environ.get( 'JDE_HUB', 'http://127.0.0.1:1967' )
SSL = os.path.join( os.environ.get('JDE_DATA', '/var/lib/Jde-Cpp'), 'DemoSeed', 'ssl' )
SEED = 'demo-seed'
RIGHTS = { 'Create': 1, 'Read': 2, 'Update': 4, 'Delete': 8, 'Purge': 16, 'Administer': 32, 'Subscribe': 64, 'Execute': 128 } #libs/access/config/release.mutation's shifts
#What a visitor reads (docs/demo/spec.md *Guests*):  what the pages read with every resource enforced, and nothing else - no
#users, sessions or logs.  A name that is no resource is skipped and said:  the app's `connections` is gated by none today.
VISITOR = {
	'gateway': [ 'serverConnections', 'histTemplates', 'histTemplateNodes', 'histGroups', 'histGroupNodes', 'oeeCodings', 'oeeFloorShapes',
		'oeeFloors', 'oeeIntervals', 'oeeLineMembers', 'oeeLines', 'oeeMachineSignals', 'oeeMachines', 'oeeReasons', 'oeeStops' ],
	'app': [ 'connections', 'calendarExceptions', 'calendarPatterns', 'calendarShifts', 'calendars' ]
}
#Each PLC's server looks a certificate's user up as its own hub user (libs/opc/server/src/UAAccess.cpp), a read of `users`.
PLC = { 'access': ['users'] }

def die( message ):
	print( f'seed: {message}', file=sys.stderr )
	sys.exit( 1 )
def say( message ): print( f'seed: {message}' )
def b64( b ): return base64.urlsafe_b64encode( b ).rstrip( b'=' ).decode()

#The certificate jwt the apps present (libs/app/client/appClient.cpp getJwt):  RS256 over {iat, host, sub, name, slug, x5c}, signed
#by the certificate's key, the certificate carried as x5c and checked against access.trustedCertDirs.  It carries no `exp`, so
#the hub takes it only fresh:  one is minted for each try.
def jwt( cert, key ):
	der = subprocess.check_output( ['openssl', 'x509', '-in', cert, '-outform', 'DER'] )
	head = b64( json.dumps({'alg': 'RS256', 'typ': 'JWT'}, separators=(',', ':')).encode() )
	body = b64( json.dumps({'iat': int(time.time()), 'host': '', 'sub': 0, 'name': SEED, 'slug': SEED, 'x5c': b64(der)}, separators=(',', ':')).encode() )
	signature = subprocess.check_output( ['openssl', 'dgst', '-sha256', '-sign', key, '-binary'], input=f'{head}.{body}'.encode() )
	return f'{head}.{body}.{b64(signature)}'

#The session id comes back in the response's Authorization header.  A hub that is not listening yet, just restarted, is waited for.
def login():
	cert, key = f'{SSL}/certs/{SEED}.pem', f'{SSL}/private/{SEED}.pem'
	if not os.path.isfile( cert ) or not os.access( key, os.R_OK ):
		die( f"no certificate at {cert}, or its key is not readable - `seed.sh certs` makes the pair, and root alone reads the key" )
	for _ in range( 60 ):
		request = urllib.request.Request( HUB+'/login', method='POST', headers={'Authorization': 'Bearer '+jwt(cert, key)} )
		try:
			with urllib.request.urlopen( request, timeout=30 ) as response:
				if session := response.headers.get( 'Authorization' ):
					return session
				die( 'the login answered no session' )
		except urllib.error.HTTPError as e:
			die( f"the hub refused the seed's login, {e.code} {e.read().decode(errors='replace')} - the hub runs the demo's config, and {SSL}/certs is among args/demo's access.trustedCertDirs?" )
		except OSError:
			time.sleep( 1 )
	die( f'nothing answers at {HUB} - systemctl status jde-opchub' )

class Hub:
	def __init__( self ): self.session = login()
	def ql( self, query, **variables ): #the answer's data
		request = urllib.request.Request( HUB+'/graphql', data=json.dumps({'query': query, 'variables': variables}).encode(), headers={'Content-Type': 'application/json', 'Authorization': self.session} )
		try:
			with urllib.request.urlopen( request, timeout=60 ) as response:
				answer = json.loads( response.read() )
		except urllib.error.HTTPError as e:
			die( f"{query} {variables}:  {e.code} {e.read().decode(errors='replace')}" )
		if not isinstance( answer, dict ) or 'errors' in answer:
			die( f'{query} {variables}:  {answer}' )
		return answer['data']
	def logout( self ):
		try: urllib.request.urlopen( urllib.request.Request(HUB+'/logout', method='POST', headers={'Authorization': self.session}), timeout=30 ).close()
		except OSError: pass

	#the row of that slug, deleted or not, restored where it is deleted;  None where there is none
	def find( self, table, slug ):
		rows = self.ql( table+'( slug:$slug ){ id deleted }', slug=slug )[table]
		if rows and rows[0]['deleted']:
			self.ql( f'mutation restore{table[0].upper()}{table[1:-1]}( id:$id )', id=rows[0]['id'] )
			say( f"{table} '{slug}':  restored" )
		return rows[0]['id'] if rows else None
	def ensure( self, table, slug, name, description ):
		if id := self.find( table, slug ):
			return id
		id = self.ql( f'mutation create{table[0].upper()}{table[1:-1]}( slug:$slug, name:$name, description:$description ){{ id }}', slug=slug, name=name, description=description )
		say( f"{table} '{slug}':  created" )
		return next( iter(id.values()) )['id']
	def member( self, user, userName, role, roleName ):
		if any( r['role'] and r['role']['id']==role for r in self.ql('acl( identityId:$id ){ role{ id } }', id=user)['acl'] ):
			return
		self.ql( 'mutation createAcl( identity:{ id:$user }, role:{ id:$role } )', user=user, role=role )
		say( f"'{userName}' is in '{roleName}'" )
	#Read on each resource named, for those that exist:  addRole refuses a name that is none, 404, which would end the seed.
	def reads( self, role, wanted ):
		resources = { (r['schemaName'], r['slug']) for r in self.ql('resources{ schemaName slug criteria }')['resources'] if not r['criteria'] }
		has = { (p['resource']['schemaName'], p['resource']['slug']) for p in self.ql('roles( slug:$slug ){ permissionRights{ resource{ schemaName slug } } }', slug=role)['roles'][0]['permissionRights'] }
		for schema, slugs in wanted.items():
			for slug in slugs:
				if (schema, slug) not in resources:
					say( f"'{role}':  skipped {schema}/{slug}, no such resource" )
				elif (schema, slug) not in has:
					self.ql( 'mutation addRole( slug:$role, permissionRight:{ allowed:$allowed, denied:0, resource:{ schemaName:$schema, slug:$slug } } )', role=role, allowed=RIGHTS['Read'], schema=schema, slug=slug )
					say( f"'{role}' reads {schema}/{slug}" )

def owner( hub ):
	if id := hub.find( 'roles', 'owner' ):
		return id
	die( "the hub has no `owner` role:  its role seed, access.roles, did not apply - journalctl -u jde-opchub" )

def access( hub, plcWait ):
	#owner first, while every resource is unenforced:  the first grant, which the hub leaves to the first user (apps/OpcHub/README.md, First run).
	#The shipped role holds every right on every resource (libs/access/config/release.roles).
	hub.member( hub.find('users', SEED), SEED, owner(hub), 'owner' )
	#the package's bundled OPC UA server, whose unit the demo leaves disabled, and the flow server, which the package's first start
	#made a row for and the demo's config runs none of (Opc.Hub.Demo.jsonnet):  each connection would be a dead card
	for slug in ( 'OpcServer', 'flow-server' ):
		for connection in hub.ql( 'serverConnections( slug:$slug ){ id }', slug=slug )['serverConnections']:
			hub.ql( 'mutation purgeServerConnection( id:$id )', id=connection['id'] )
			say( f"connection '{slug}':  purged" )
	guest = hub.ensure( 'users', 'guest', 'Guest', "The public demo's visitor:  every session nobody signed in to (access.guest)." )
	visitor = hub.ensure( 'roles', 'visitor', 'Visitor', "The public demo's guest:  reads the plant's pages and changes nothing.  No users, sessions or logs, which Viewer reads." )
	hub.reads( 'visitor', VISITOR )
	hub.member( guest, 'guest', visitor, 'visitor' )
	plcs = hub.ensure( 'roles', 'demo-plc', 'Demo PLC', "The public demo's PLCs:  Read on users, which each PLC's server looks a certificate's user up with, as its own." )
	hub.reads( 'demo-plc', PLC )
	waited = time.monotonic()+plcWait #a PLC's user is enrolled by its first login, a few seconds into its unit's start
	while time.monotonic()<waited and len( hub.ql('users( slug:$slugs ){ id }', slugs=[f'plc{n}' for n in range(1, 6)])['users'] )<5:
		time.sleep( 1 )
	for n in range( 1, 6 ):
		if plc := hub.find( 'users', f'plc{n}' ):
			hub.member( plc, f'plc{n}', plcs, 'demo-plc' )
		else:
			say( f"plc{n} has not logged in yet:  once jde-plc@{n} runs, `seed.sh access` again puts it in 'demo-plc'" )
	#last:  a resource's row restored is the resource enforced, and from here a call takes a grant
	for resource in hub.ql( 'resources{ id schemaName slug deleted }' )['resources']:
		if resource['deleted']:
			hub.ql( 'mutation restoreResource( id:$id )', id=resource['id'] )
			say( f"{resource['schemaName']}/{resource['slug']}:  enforced" )

def grant( hub, email ):
	users = [ u for u in hub.ql('users{ id loginName provider deleted }')['users'] if u['provider']=='Google' and (u['loginName'] or '').lower()==email.lower() and not u['deleted'] ]
	if not users:
		die( f"no Google user '{email}':  it is enrolled by its first sign-in, through the tunnel (README.md, The first sign-in)" )
	hub.member( users[0]['id'], email, owner(hub), 'owner' )

if __name__=='__main__':
	parser = argparse.ArgumentParser( prog='seed.py' )
	commands = parser.add_subparsers( dest='command', required=True )
	commands.add_parser( 'access' ).add_argument( '--plcs', type=int, default=0, metavar='seconds' )
	commands.add_parser( 'grant' ).add_argument( 'email' )
	commands.add_parser( 'session' )
	args = parser.parse_args()
	hub = Hub()
	if args.command=='session':
		print( hub.session )
	else:
		try:
			access( hub, args.plcs ) if args.command=='access' else grant( hub, args.email )
		finally:
			hub.logout()
		say( f'{args.command}:  done' )