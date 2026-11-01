import { LiveAnnouncer } from '@angular/cdk/a11y';
import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatTabsModule } from '@angular/material/tabs';

import { DetailActions, DetailPage, errorMessage, isRefusal, PickOption, Properties} from 'jde-framework';
import { Latest } from 'jde-spa';

import { ServerProperties } from './server-properties/server-properties';
import { ConnectionSettingName, connectionSettingNames, securedPolicies, securityModes, securityPolicyNone, ServerCnnctn, ServerCnnctnProps } from '../../../model/server-cnnctn';
import { modeName, policyName } from '../../../model/gateway-endpoint';
import { Gateway } from '../../../services/gateway-service';
import { GatewayDetailData } from '../../../services/resolvers/client-resolver';
import { namespaceColumns, Server, serverDescColumns, TestConnectionResult } from '../../../model/server';
import { OpcStore } from '../../../services/opc-store';

//what a connect depends on, as the test and the save send it:  the three the certificate and the credential key on, and the spec's
//connection settings (docs/clients/spec.md *Connection settings*), null where the form leaves one to the gateway
type ConnectSettings = { slug:string; url:string; certificateUri:string } & Record<ConnectionSettingName,string|number|null>;
const connectSettings = ( p:Partial<ServerCnnctn> ):ConnectSettings=>({ slug: p.slug?.trim() ?? "", url: p.url?.trim() ?? "", certificateUri: p.certificateUri?.trim() ?? "", ...ServerCnnctn.settings(p) });
const settingLabels:Record<string,string> = { url: "URL", certificateUri: "Certificate URI", defaultBrowseNs: "Default Namespace", securityMode: "Security Mode", securityPolicy: "Security Policy", sessionTimeout: "Session Timeout", requestTimeout: "Request Timeout", pingInterval: "Ping Interval", ttl: "Idle TTL", locale: "Locales" };

@Component( {
	templateUrl: './client-detail.html',
	//the trailing class is load-bearing:  Angular hashes a component's *shape* into its style-encapsulation id and leaves
		//the class name out, so four routed pages that now share DetailPage and this host string could collide with NG0912.
		host: {class:'main-content my-content client-detail'},
	imports: [DetailActions, MatButtonModule, MatTabsModule, Properties, ServerProperties],
})
export class ClientDetail extends DetailPage<ServerCnnctn>{
	constructor(){ super( 'client-detail' ); }

	protected override get ctor(){ return ServerCnnctn; }
	protected override onRow(){//no child collections - the connection is its properties;  the Connection tab opens on the resolver's describe, until a test replaces it
		this.gateway = (<GatewayDetailData<ServerCnnctn>>this.pageData).gateway;
		this.server.set( this.row.server );
		this.serverError.set( this.row.serverError );
		this.#resultFor.set( this.row.server || this.row.serverError ? connectSettings(this.row) : undefined );
		this.testing.set( undefined );
		this.#rowLoads.next();
	}
	protected override upsert():ServerCnnctn{
		return new ServerCnnctn( {
			...this.properties(),
		} as ServerCnnctnProps);
	}
	protected override afterMutate(){//the node pages answer from OpcStore's describe:  an edit has to reach them, a delete end them (reviews/m3-closing.md #5)
		if( this.row.slug )
			this.#opcStore.forget( this.gateway.slug, this.row.slug );
	}
	protected override get title(){ return this.row.name ? `${this.row.name} - Connection` : "New Connection"; }
	override get ql(){ return this.gateway; }//per-gateway, not a single injected service - the row's, from ClientResolver

	//what a user reads first, then the connection, then the two technical fields, then the spec's settings in the order the channel
	//is built:  the mode and policy, the session's timeouts, the gateway's own keep-alive and TTL, the locales - the alphabetical
	//default buried Description between them and put Url last
	readonly fieldOrder = ["slug", "name", "description", "url", "certificateUri", "defaultBrowseNs", ...connectionSettingNames];
	readonly fieldLabels = settingLabels;//the camelCase split gives "Url", "Certificate Uri", "Default Browse Ns", "Ttl"
	//The mode and policy picks, the unset option labelled with what unset means to the gateway:  the floor the Certificate URI sets
	//for the mode, and for the policy the strongest the two sides share.  The policies are the ones the gateway carries;  it refuses
	//any other at save.  Over the floor alone, whose string equality keeps a keystroke elsewhere from rebuilding the form's fields.
	readonly #floor = computed( ()=>(this.properties() as Partial<ServerCnnctn>|null)?.certificateUri?.trim() ? "SignAndEncrypt" : "None" );
	readonly picks = computed<{ securityMode:PickOption[]; securityPolicy:PickOption[] }>( ()=>({
		securityMode: [ {value: null, label: `Default (${modeName(this.#floor())})`}, ...securityModes.map( m=>({value: m, label: modeName(m)}) ) ],
		securityPolicy: [ {value: null, label: "Default (strongest shared)"}, {value: securityPolicyNone, label: "None"}, ...securedPolicies.map( uri=>({value: uri, label: policyName(uri)}) ) ]
	}) );
	readonly units = { sessionTimeout: "ms", requestTimeout: "ms", pingInterval: "ms", ttl: "ms" };
	readonly hints = {
		sessionTimeout: "Empty:  the client library's 20 minutes.  The server may shorten it.",
		requestTimeout: "Empty:  the gateway's requestTimeout setting.  Also how long Test Connection waits.",
		pingInterval: "Empty:  the gateway's pingInterval setting, or half the session timeout where that is shorter.  0 turns the keep-alive off.",
		ttl: "Empty:  the gateway's ttl setting - how long an idle session is kept open.",
		locale: "Comma-separated locale ids, en-US,de.  Empty sends none."
	};
	get serverCnnctn(){ return this.row; }//the template's name for it

	//Test Connection:  the form's values, saved or not, connected by the gateway as the saved row would be (testConnection) and
	//shown in the Connection tab's states - a wrong url or an untrusted certificate says why there, and a saved connection whose
	//page loaded Not connected is retried without a reload.  The base's `!id` clamp still lands a new row on Properties.
	//Enabled on what the gateway needs - a url, and a slug that passes the insert's rule, since the slug names the certificate the
	//gateway presents and the credential this session holds, and settings the gateway would save - and never twice at once.
	readonly canTest = computed( ()=>{
		const p = this.properties() as ServerCnnctn|null;
		return !this.testing() && !!p?.url?.trim() && !!p?.slug?.trim() && !p.fieldError( "slug" ) && !connectionSettingNames.some( n=>p.fieldError(n) );
	} );
	async onTestClick(){
		if( !this.canTest() )
			return;//disabledInteractive:  the disabled button still takes clicks.
		const p = this.properties() as ServerCnnctn;
		const settings = connectSettings( p );
		const rowLoad = this.#rowLoads.current;
		this.testing.set( settings.url );
		this.tabIndex.set( 1 );//the result lands on the Connection tab, and it shows the connect in flight
		this.#announcer.announce( `Connecting to ${settings.url}.` );
		let server:Server|undefined, error:string|undefined, refused:unknown;
		try{
			const { slug, ...vars } = settings;
			const data = await this.gateway.postQL<{testConnection:TestConnectionResult}>( ClientDetail.testMutation, {opc: slug, ...vars} );
			const result = data.testConnection;
			server = new Server({ connection: {...p, ...settings} as ServerCnnctnProps, desc: result, policy: result.securityPolicyUri, mode: result.securityMode, namespaces: result.namespaces });
		}
		catch( e ){//the tab's not-connected state carries the reason - no toast over it
			if( isRefusal(e) )
				refused = e;
			else
				error = errorMessage(e);
		}
		if( !this.#rowLoads.is(rowLoad) )
			return;//the router reuses this page:  the answer is for a connection no longer shown.
		this.testing.set( undefined );
		//testConnection needs Update, which a viewer lacks:  a refusal says nothing about the server, so the tab keeps what it showed
		//and the denial is a toast, as Save's is.
		if( refused ){
			this.snackbar.exception( "Test Connection was refused.", refused );
			return;
		}
		this.server.set( server );
		this.serverError.set( error );
		this.#resultFor.set( settings );
		this.#announcer.announce( server ? `Connected to ${server.applicationName || settings.url}.` : `Not connected:  ${error}` );
	}
	//a mutation, POSTed:  it connects the gateway to the form's url and issues the slug's certificate, which a GET must not do.  The
	//settings go as the save would send them, null where the form leaves one unset, so the test tests the connection Save would save.
	static readonly testMutation = `mutation testConnection( opc: $opc, url: $url, certificateUri: $certificateUri, ${connectionSettingNames.map( n=>`${n}: $${n}` ).join( ", " )} ){ ${serverDescColumns} securityPolicyUri securityMode namespaces{ ${namespaceColumns} } }`;
	//what the Connection tab shows:  the resolver's describe or its failure (onRow), then the last test's.  A new connection has
	//neither until it is tested, and the tab says what the button does rather than why a session that was never tried is missing.
	server = signal<Server|undefined>( undefined );
	serverError = signal<string|undefined>( undefined );
	get untested():boolean{ return this.isNew && !this.server() && !this.serverError(); }//a getter, not a computed:  `row` is no signal, and the router reuses this page across connections
	testing = signal<string|undefined>( undefined );//the url under test while a Test Connection is in flight
	//the settings the shown result was for, where the form no longer holds them;  unset while the form matches
	readonly staleFor = computed( ()=>{
		const was = this.#resultFor(), p = this.properties();
		if( !was || !p )
			return undefined;
		const now = connectSettings( p );
		const changed = [
			was.url==now.url ? "" : `URL ${was.url}`,
			was.certificateUri==now.certificateUri ? "" : was.certificateUri ? `Certificate URI ${was.certificateUri}` : "an empty Certificate URI",
			was.slug==now.slug ? "" : `slug ${was.slug}`,
			...connectionSettingNames.map( n=>was[n]===now[n] ? "" : was[n]===null ? `an unset ${settingLabels[n]}` : `${settingLabels[n]} ${n=="securityMode" ? modeName(String(was[n])) : n=="securityPolicy" ? policyName(String(was[n])) : was[n]}` )
		].filter( (x)=>x );
		return changed.length ? changed.join( ", " ) : undefined;
	} );
	#resultFor = signal<ConnectSettings|undefined>( undefined );
	#rowLoads = new Latest();//onRow's:  a test answers only for the row it started on.
	#opcStore = inject( OpcStore );
	//the test's start and end, for a screen reader:  a region inserted with its text may go unannounced, and the tab's content -
	//a live region there included - is created with its text when the press switches to it.
	#announcer = inject( LiveAnnouncer );
	//the row's, set in onRow:  the router reuses this page across gateways
	gateway!:Gateway;
}