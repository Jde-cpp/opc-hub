import { Service } from '@angular/core';

declare const google: any;

export const googleClientIdKey = 'googleAuthClientId';
export const googleLoginHintKey = 'googleLoginHint';
export const googleSignedOutKey = 'googleSignedOut';//a sign-out the sign-in library has yet to hear of (signOut)
//Google's sign-in library.  No page carries it:  index.html did, with the platform library beside it, which nothing used, so
//every page of every site asked Google for both, one whose server offers no Google sign-in included.
export const googleSignInScript = 'https://accounts.google.com/gsi/client';

//Owns the Google Identity Services machinery (initialize/renderButton/prompt) so a silent prompt() can run from any
//page, not just /login (reviews/todo.md §7). The login page wires `credentialHandler` for interactive sign-ins;
//ProtoService calls renewCredential() when a 401 says the session lapsed mid-use.
@Service()
export class GoogleAuthService{
	//interactive credential sink, set by the login page while it is showing. A credential produced by a pending
	//renewCredential() settles that renewal instead - the two flows never race for one credential.
	credentialHandler?: ( credential:string )=>void;

	//the sign-in library has loaded:  false until initialize, which injects it, has seen it load.
	get available():boolean{ return this.#loaded && typeof google!="undefined" && !!google; }

	//Injects Google's sign-in library the first time it is given a client id, which only a server that offers the sign-in reports,
	//and sets it up once it has loaded.  Rejects where it does not load, and the next call injects it again.
	async initialize( clientId:string ):Promise<void>{
		if( !clientId )
			throw new Error( "no google client id" );
		await ( this.#scripts ??= this.#load().catch( e=>{ this.#scripts = undefined; throw e; } ) );
		if( !this.available )
			throw new Error( "google not defined" );
		localStorage.setItem( googleClientIdKey, clientId );//the silent-renewal path needs it on page loads that never visit /login
		if( this.#initializedClientId==clientId )
			return;//idempotent - the callback is always #onCredential, so nothing changes on a re-init
		google.accounts.id.initialize({
			client_id: clientId,
			auto_select: true,
			button_auto_select: true,
			cancel_on_tap_outside: true,
			callback: ( response:any )=>this.#onCredential( response.credential ),
			native_callback: ( response:any )=>this.#onCredential( response.credential ),
			login_hint: localStorage.getItem( googleLoginHintKey )
		});
		this.#initializedClientId = clientId;
		if( localStorage.getItem(googleSignedOutKey) ){
			localStorage.removeItem( googleSignedOutKey );
			google.accounts.id.disableAutoSelect();
		}
	}

	//A Google user's sign-out.  /login's prompt runs with auto_select, so the library would sign them straight back in until told
	//not to:  disableAutoSelect, which it keeps in Google's own cookie.  Where it has not loaded on this page, a session signed in
	//on an earlier load, the next initialize tells it, before any prompt.
	signOut():void{
		if( this.available )
			google.accounts.id.disableAutoSelect();
		else
			localStorage.setItem( googleSignedOutKey, "true" );
	}

	renderButton( parent:HTMLElement|null, options:object={theme:"outline", size:"large"} ):void{
		if( !this.available )
			throw new Error( "google not defined" );
		google.accounts.id.renderButton( parent, options );
	}

	prompt():void{//auto_select only takes effect during prompt() - silently re-signs-in a returning user
		google.accounts.id.prompt();
	}

	//a script element, settled by its load or its failure - a spec's override settles without one
	protected loadScript( src:string ):Promise<void>{
		return new Promise<void>( ( resolve, reject )=>{
			const script = document.createElement( "script" );
			script.src = src;
			script.async = true;
			script.onload = ()=>resolve();
			script.onerror = ()=>reject( new Error(`'${src}' did not load.`) );
			document.head.appendChild( script );
		} );
	}
	async #load():Promise<void>{
		await this.loadScript( googleSignInScript );
		this.#loaded = true;
	}

	//Silent renewal: resolves the fresh credential, or null when the prompt produced none - FedCM cooldown, multiple
	//Google sessions, or a dismissal are normal outcomes, never errors. Concurrent callers coalesce into one prompt.
	renewCredential():Promise<string|null>{
		if( this.#renewal )
			return this.#renewal;
		const clientId = localStorage.getItem( googleClientIdKey );
		if( !clientId )
			return Promise.resolve( null );//no cached client id means the login page has never run in this browser - nothing to renew with
		let resolve!:( credential:string|null )=>void;
		const renewal = new Promise<string|null>( r=>resolve=r );
		this.#renewal = renewal;
		const settle = ( credential:string|null )=>{
			if( this.#renewal!==renewal )
				return;//already settled (e.g. the timeout raced the moment callback)
			this.#renewal = null;
			this.#renewalSettle = undefined;
			clearTimeout( timer );
			resolve( credential );
		};
		this.#renewalSettle = settle;
		const timer = setTimeout( ()=>settle(null), GoogleAuthService.renewalTimeoutMs );//backstop for FedCM paths that fire no moment callback at all, and for scripts that never settle
		this.initialize( clientId ).then( ()=>{
			if( this.#renewal!==renewal )
				return;//the backstop settled it while the scripts loaded
			google.accounts.id.prompt( ( moment:any )=>{
				//FedCM deprecated the display-moment queries, so only skip/dismiss are inspected. 'credential_returned'
				//is a dismissal that precedes success - the credential callback settles that one.
				if( moment.isSkippedMoment?.() || (moment.isDismissedMoment?.() && moment.getDismissedReason?.()!="credential_returned") ){
					console.debug( `silent google prompt yielded no credential - skipped: '${moment.getSkippedReason?.() ?? ""}', dismissed: '${moment.getDismissedReason?.() ?? ""}'.` );//normal outcome (auto-reauth cooldown, multiple sessions, dismissal), but say why for diagnosis
					settle( null );
				}
			});
		} ).catch( e=>{
			console.warn( "silent google prompt failed.", e );
			settle( null );
		} );
		return renewal;
	}

	#onCredential( credential:string ):void{
		const settle = this.#renewalSettle;
		if( settle )
			settle( credential );
		else if( this.credentialHandler )
			this.credentialHandler( credential );
		else
			console.warn( "google credential arrived with no handler - ignored." );
	}

	#scripts?:Promise<void>;
	#loaded = false;
	#initializedClientId?:string;
	#renewal:Promise<string|null>|null = null;
	#renewalSettle?:( credential:string|null )=>void;
	static renewalTimeoutMs = 15_000;
}