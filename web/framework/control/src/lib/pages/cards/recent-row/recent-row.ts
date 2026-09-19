import { Component, inject } from '@angular/core';
import { RouterLink } from '@angular/router';
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
	recent = this.#visits.visits;
	#now = Date.now();//"12 min ago" is measured from when the page opened, not re-ticked while it stays open
}
