import { Component, computed, inject } from '@angular/core';
import { Router, RouterLink } from '@angular/router';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { RecentVisits, timeAgo } from 'jde-spa';

//A landing page's Recently visited row:  the pages RecentVisits kept, as small cards tinted by the section each sits under.
//Its own component, not more of Cards:  the one stylesheet had outgrown the 8 kB per-component budget.
@Component( {
	selector: 'recent-row',
	templateUrl: './recent-row.html',
	styleUrls: ['./recent-row.scss'],
	imports: [MatButtonModule, MatIconModule, RouterLink]
})
export class RecentRow{
	constructor(){
		this.#visits.load();//the signed-in user's saved list;  recent() fills in when it lands
	}
	ago( at:number ):string{ return timeAgo( at, this.#now ); }
	clear():void{ this.#visits.clear(); }
	#visits = inject( RecentVisits );
	#router = inject( Router );
	//A visit is kept as the url the router serialized - percent-encoded - and a string routerLink splits that on '/' and takes
	//each part as a raw segment, so the '%' was encoded again and a node under `Simulation Examples` never reopened
	//(reviews/m3-closing.md #8).  So the link is that url parsed back into a tree;  a computed, so the tree is not a fresh input
	//every change-detection pass.  Rows saved before the fix are repaired the same way.
	recent = computed( ()=>this.#visits.visits().map( v=>({...v, link: this.#router.parseUrl(v.url)}) ) );
	#now = Date.now();//"12 min ago" is measured from when the page opened, not re-ticked while it stays open
}
