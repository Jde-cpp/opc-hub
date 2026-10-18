import { HttpClient } from '@angular/common/http';
import { Component, inject, model, OnDestroy, OnInit, signal } from '@angular/core';
import { DomSanitizer, SafeHtml } from '@angular/platform-browser';
import { ActivatedRoute, Router } from '@angular/router';
import { firstValueFrom, Subscription } from 'rxjs';
import { ComponentPageTitle } from '../component-page-title/component-page-title';
import { RouteItem } from '../component-sidenav/route-item';
import { IENVIRONMENT } from '../../services/environment/environment';
import { HELP_TOPICS, HelpTopic, helpTopics } from '../../services/help/help-topic';

//Renders one help topic's markdown.  The markdown is the app's own shipped asset - the same trust as the bundle - so the html
//bypasses Angular's sanitizer, which would otherwise strip the heading ids the in-page anchors need.  marked is imported
//dynamically and nowhere else:  statically it would join this page's chunk, which every tab preloads (see web/CLAUDE.md).
@Component({
	selector: 'help-page',
	templateUrl: './help-page.html',
	styleUrls: ['./help-page.scss'],
	host: {class: 'main-content mat-drawer-container my-content'}
})
export class HelpPage implements OnInit, OnDestroy{
	ngOnInit(){
		this.#subscriptions.add( this.route.data.subscribe( data=>this.#load( <HelpTopic|undefined>data['topic'] ) ) );
		this.#subscriptions.add( this.route.fragment.subscribe( fragment=>{
			this.#fragment = fragment;
			if( this.html() )
				this.#scrollTo( fragment );
		}) );
	}
	ngOnDestroy(){ this.#subscriptions.unsubscribe(); }

	async #load( topic:HelpTopic|undefined ){
		const request = ++this.#request;
		const id = this.route.snapshot.paramMap.get( 'topic' ) ?? '';
		this.#setSideNav( topic, id );
		this.html.set( null );
		this.error.set( null );
		this.componentPageTitle.title = topic?.title ?? 'Help';
		if( !topic ){
			this.error.set( `There is no help topic '${id}'.` );
			return;
		}
		try{
			const [source, {Marked}] = await Promise.all( [firstValueFrom( this.http.get(topic.url, {responseType: 'text'}) ), import('marked')] );
			if( request!=this.#request )//navigated on while this one was loading
				return;
			const ids = new Map<string,number>();
			const md = new Marked({ renderer: {
				heading( {tokens, depth, text} ){//marked dropped its own heading ids in v8; the slug is github's shape, deduplicated with -1, -2...
					let id = text.toLowerCase().trim().replace( /[^\w]+/g, '-' ).replace( /^-|-$/g, '' ) || 'section';
					const seen = ids.get( id ) ?? 0;
					ids.set( id, seen+1 );
					if( seen )
						id = `${id}-${seen}`;
					return `<h${depth} id="${id}">${this.parser.parseInline( tokens )}</h${depth}>\n`;
				}
			}});
			const html = await md.parse( this.#substitute(source) );
			this.html.set( this.sanitizer.bypassSecurityTrustHtml(html) );
			setTimeout( ()=>this.#scrollTo( this.#fragment ) );//once the innerHTML binding has rendered
		}
		catch( e ){
			if( request==this.#request )
				this.error.set( `Could not load '${topic.url}': ${(<Error>e)?.message ?? e}` );
		}
	}
	//{{version}} and any other string the site's environment carries;  anything unresolved is left as written.
	#substitute( source:string ):string{
		return source.replace( /\{\{(\w+)\}\}/g, (match, key)=>{
			const value = this.environment?.get<unknown>( key );
			return typeof value=='string' ? value : match;
		});
	}
	#setSideNav( topic:HelpTopic|undefined, id:string ){
		const siblings = this.#topics.map( t=>new RouteItem({path: t.id, title: t.title, icon: t.icon}) );
		this.sideNav.set( new RouteItem({ path: id, title: topic?.title ?? id, parent: new RouteItem({path: '/help', title: 'Help'}), siblings }) );//parent absolute:  ComponentNav renders parent.path + '/' + sibling.path
	}
	#scrollTo( fragment:string|null ){
		if( fragment )
			document.getElementById( fragment )?.scrollIntoView( {block: 'start'} );
	}
	//Links inside the markdown:  an app route goes through the router (an <a href="/gateways"> would reload the document and
	//drop the sockets), a '#slug' scrolls in place (with <base href="/"> the browser would resolve it to /#slug and leave the
	//page), and an external url opens beside the app.  Modified clicks are the browser's.
	onClick( event:MouseEvent ){
		const anchor = (<HTMLElement>event.target).closest?.( 'a[href]' ) as HTMLAnchorElement|null;
		if( !anchor || event.button!=0 || event.ctrlKey || event.metaKey || event.shiftKey || event.altKey || anchor.target=='_blank' )
			return;
		const href = anchor.getAttribute( 'href' ) ?? '';
		if( href.startsWith('#') ){
			event.preventDefault();
			this.#scrollTo( decodeURIComponent(href.substring(1)) );
		}
		else if( href.startsWith('/') ){
			event.preventDefault();
			this.router.navigateByUrl( href );
		}
		else if( /^https?:\/\//i.test(href) ){
			event.preventDefault();
			window.open( href, '_blank', 'noopener' );
		}
	}

	html = signal<SafeHtml|null>( null );
	error = signal<string|null>( null );
	sideNav = model<RouteItem>();//the sidenav hands its own model over on activation (ComponentSidenav.onRouterOutletActivate)
	#fragment:string|null = null;
	#request = 0;
	#subscriptions = new Subscription();
	#topics = helpTopics( inject(HELP_TOPICS, {optional: true}) );
	private route = inject( ActivatedRoute );
	private router = inject( Router );
	private http = inject( HttpClient );
	private sanitizer = inject( DomSanitizer );
	private componentPageTitle = inject( ComponentPageTitle );
	private environment = inject( IENVIRONMENT, {optional: true} );
}