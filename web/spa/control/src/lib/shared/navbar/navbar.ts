//https://github.com/angular/components/blob/a55b19797f0bccf467d5602f526eef236737498b/docs/src/app/shared/navbar/navbar.ts
import {Component, computed, ElementRef, inject, OnInit, signal, viewChild} from '@angular/core';
import { toSignal } from '@angular/core/rxjs-interop';
import {FormControl, ReactiveFormsModule} from '@angular/forms';
import {MatAutocompleteModule, MatAutocompleteTrigger} from '@angular/material/autocomplete';
import {MatButtonModule} from '@angular/material/button';
import {MatFormFieldModule} from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatMenuModule } from '@angular/material/menu';
import { MatTooltipModule } from '@angular/material/tooltip';
import {ActivatedRoute, NavigationEnd, RouterLink, RouterLinkActive} from '@angular/router';
import {Route, Router, Routes} from '@angular/router';
import { Title } from '@angular/platform-browser';
import { debounceTime, distinctUntilChanged, filter, map, of, switchMap } from 'rxjs';
import {NavigationFocusService} from '../navigation-focus/navigation-focus-service';
import {ThemePicker} from '../theme-picker/theme-picker';
import { Authorization } from '../authorization/authorization';
import { Breadcrumbs } from './breadcrumbs/breadcrumbs';
import { Favorites } from './favorites/favorites-dialog';
import { ProfileStore } from '../../services/profile/profile-store';
import { RouteStore } from '../../services/route-store';
import { RouteItem } from '../../pages/component-sidenav/route-item';
import { matchConfig, segmentDisplay } from '../../services/route-utils';
import { SearchService } from '../../services/search/search-service';
import { SearchResult } from '../../services/search/search-provider';
import { HELP_TOPICS, helpTopicFor, helpTopics } from '../../services/help/help-topic';
import { APP_LOGO, APP_NAME } from '../../services/document-title';
import { RecentVisits, SEGMENT_NAME } from '../../services/recent-visits';

export type Favorite={
	folderName?:string;
	name:string;
	route:string;
	queryParams?:Record<string,unknown>;
}
export type Folder = { folderName:string, items:Favorite[] };
@Component({
  selector: 'app-navbar',
  templateUrl: './navbar.html',
  styleUrls: ['./navbar.scss'],
  host: {
    '(document:keydown)': 'onKeydown($event)',
  },
  imports: [
    Authorization,
		Breadcrumbs,
    Favorites,
		MatAutocompleteModule,
    MatButtonModule,
		MatFormFieldModule,
    MatIconModule,
		MatInputModule,
    MatMenuModule,
		MatTooltipModule,
		ReactiveFormsModule,
    RouterLink,
    RouterLinkActive,
    ThemePicker
]
})
export class NavBar implements OnInit {
  skipLinkHref: string | null | undefined;
  skipLinkHidden = true;
  private navigationFocusService:NavigationFocusService = inject( NavigationFocusService );
  constructor() {
    this.defaultFavorites = this.router.config.filter( x=>
			x.path!="login"
			&& x.path!.indexOf(':slug')==-1
			&& !x.path!.includes('/')
			&& ( !x.children || x.children.find( y=>!y.path!.length) )
		).map( x=>({ name: x.title as string, route: '/'+x.path } ));
  }
	async ngOnInit(){
		this.router.events.pipe(//subscribe before the await:  the load defers the rest of ngOnInit past the initial NavigationEnd.
			filter( (e)=> e instanceof NavigationEnd )
		).subscribe( (e:NavigationEnd)=>{
			const path = e.urlAfterRedirects.split( /[?#]/ )[0];//query AND fragment - help pages link to '#section' anchors, which are not a segment
			const crumbs = this.#buildCrumbs( path );
			this.crumbs.set( crumbs );
			this.name.set( crumbs[crumbs.length-1].title );
			this.route.set( path );
			this.#recentVisits.visit( path, crumbs );//the home page's Recently visited row names each page as its crumbs do
		});
		this.favorites.set( await this.#profileStore.load<Favorite[]>("favorites", this.defaultFavorites) );
		this.isLoading.set( false );
	}
	asFolder(item:Favorite|Folder):Folder{
		return item as Folder;
	}
  routerLinkOptions( route:Route ):{exact:boolean}{
    return {exact:!route.path!.length};
  }
	onFavoriteChange( change:Favorite ){
		const route = this.route();
		let favs;
		if( !change ) //delete
			favs = this.favorites().filter( fav=>fav.route!=route );
		else{
			favs = [ ...this.favorites() ];
			const index = favs.findIndex( fav=>fav.route==route );
			if( index==-1 ) //add
				favs.push( { ...change, route } );
			else //edit
				favs[index] = { ...favs[index], name: change.name, folderName: change.folderName };
		}
		this.favorites.set( favs );
		this.#profileStore.save( "favorites", favs ).catch( e=>console.warn("Could not save favorites.", e) );
	}
	onToggleBreadcrumbs(){
		const show = !this.showBreadcrumbs();
		this.showBreadcrumbs.set( show );
		this.#profileStore.set( "showBreadcrumbs", show );
	}
	#buildCrumbs( path:string ):RouteItem[]{
		const segments = path.split('/').filter( s=>s.length );
		const home = this.router.config.find( c=>c.path=='' );
		const crumbs = [ new RouteItem({ path: '/', title: (home?.title as string) ?? 'Home' }) ];
		for( let i=0; i<segments.length; i++ ){
			const config = NavBar.matchConfig( this.router.config, segments.slice(0, i+1) );
			let title = config?.title as string|undefined, icon:string|undefined;
			if( !title || title.startsWith(':') ){//no title, or the ':param' substitute-the-segment convention
				const child = this.#segmentItem( segments.slice(0,i).join('/'), segments[i] );
				title = child?.title ?? this.#segmentName?.( segments, i ) ?? segmentDisplay( segments[i] );
				icon = child?.icon;//the recently visited row draws it;  the crumb itself does not
			}
			crumbs.push( new RouteItem({ path: config ? '/'+segments.slice(0,i+1).join('/') : undefined, title, icon }) );//no matching route ⇒ no path ⇒ rendered as text, not a link
		}
		return crumbs;
	}
	#segmentItem( parentUrl:string, segment:string ):RouteItem|undefined{//RouteStore writers key inconsistently: "gateways/gw1" (UrlSegments join), '/apps', bare "users"
		const last = parentUrl.split('/').pop() ?? '';
		for( const key of [parentUrl, '/'+parentUrl, last] ){
			const child = this.#routeStore.getChildren( key ).find( c=>c.path==segment || c.path?.endsWith('/'+segment) );//child paths are bare slugs or parent-prefixed
			if( child?.title )
				return child;
		}
		return undefined;
	}
	static matchConfig( routes:Routes, segments:string[] ):Route|undefined{ return matchConfig( routes, segments ); }//lives in services/route-utils now - RouteSearchProvider needs it without importing a component.

	onKeydown( event:KeyboardEvent ){//'/' jumps to search, unless the keystroke belongs to whatever the user is already typing in
		if( event.key!='/' || event.ctrlKey || event.metaKey || event.altKey || this.#isTyping(event.target) )
			return;
		event.preventDefault();//otherwise the '/' lands in the field we just focused
		this.searchInput()?.nativeElement.focus();
	}
	#isTyping( target:EventTarget|null ):boolean{
		const el = target as HTMLElement|null;
		return !!el && (el.isContentEditable || ['INPUT','TEXTAREA','SELECT'].includes(el.tagName));
	}
	//Enter with no highlighted option (the panel is closed, or nothing matched yet) goes to the first result;  with one, the
	//autocomplete trigger already selected it and marked the event handled.
	//Read defaultPrevented BEFORE preventing:  this used to call preventDefault() and then test the flag it had just set,
	//so the flag was always true, the branch below unreachable, and Enter with nothing highlighted did nothing at all.
	onSearch( event:Event ){
		const handled = event.defaultPrevented || (this.searchTrigger()?.panelOpen && this.searchTrigger()?.activeOption);
		event.preventDefault();//never submit anything
		if( handled )
			return;
		const first = this.searchResults()[0];
		if( first )
			this.onSearchSelected( first );
	}
	onSearchSelected( result:SearchResult ){
		this.router.navigate( Array.isArray(result.route) ? result.route : [result.route], {queryParams: result.queryParams} );
		this.searchForm.setValue( '' );
		this.searchTrigger()?.closePanel();
		this.searchInput()?.nativeElement.blur();
	}
	displayWith = ( result:string|SearchResult|null ):string=>typeof result=='string' ? result : result ? (result.prefix ? result.prefix+':' : '')+result.title : '';
	trackResult( result:SearchResult ):string{ return SearchService.key( result ); }
	favoriteMenus = computed( ()=>{
		let items:Array<Favorite|Folder> = [];
		if( !this.favorites() )
			return [];
		for( let fav of this.favorites() ){
			if( this.appName && !fav.folderName && fav.route=='/' )//the brand is the home link, so a top-level Home favorite would repeat it
				continue;
			if( !fav.folderName )
				items.push( fav );
			else{
				let select = items.find( x=>(x as Folder).folderName==fav.folderName ) as Folder;
				if( !select )
					items.push( {folderName: fav.folderName, items: [fav]} );
				else
					select.items.push( fav );
			}
		}
		return items;
	});
	appName = inject( APP_NAME, {optional: true} );//a site that names itself gets a brand link in place of the Home favorite
	appLogo = inject( APP_LOGO, {optional: true} );
	#profileStore = inject(ProfileStore);
	#recentVisits = inject(RecentVisits);
	#segmentName = inject( SEGMENT_NAME, {optional: true} );//a site's name for a segment no route or RouteStore child names (an opc node)
	#routeStore = inject(RouteStore);
	crumbs = signal<RouteItem[]>( [] );
	showBreadcrumbs = signal<boolean>( ProfileStore.local<boolean>("showBreadcrumbs", true) );//sync static read — awaiting #profileStore.load here would delay isLoading
	defaultFavorites:Favorite[];
	favorites = signal<Favorite[]>(null as any);
	isLoading = signal<boolean>( true );
	name = signal<string>( null as any );
	route = signal<string>( null as any );
	#helpTopics = helpTopics( inject(HELP_TOPICS, {optional: true}) );//optional:  a site without help topics still gets the index link
	//the ? button's target - the topic whose route pattern best fits the current url, the index from inside the help section itself
	helpRoute = computed<string[]>( ()=>{
		const segments = (this.route() ?? '').split( '/' ).filter( s=>s.length );
		const topic = segments[0]=='help' ? undefined : helpTopicFor( this.#helpTopics, segments );
		return topic ? ['/help', topic.id] : ['/help'];
	});
	existing = computed<Favorite|undefined>( ()=>this.favorites()?.find( fav=>fav.route==this.route() ) );// the favorite corresponding to the current route, if any
	router = inject(Router);
	searchForm = new FormControl<string|SearchResult>( '', {nonNullable: true} );//the selected option lands here as the object;  displayWith renders it.
	searchInput = viewChild<ElementRef<HTMLInputElement>>( 'searchInput' );
	searchTrigger = viewChild( MatAutocompleteTrigger );
	#searchService = inject( SearchService );
	searchResults = toSignal( this.searchForm.valueChanges.pipe(
		map( value=>typeof value=='string' ? value : '' ),
		debounceTime( 150 ),
		distinctUntilChanged(),
		switchMap( text=>text.trim().length ? this.#searchService.search( text ) : of( [] as SearchResult[] ) )
	), {initialValue: [] as SearchResult[]} );
}