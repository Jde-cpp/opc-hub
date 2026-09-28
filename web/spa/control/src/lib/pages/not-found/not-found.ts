import { Component, inject } from '@angular/core';
import { toSignal } from '@angular/core/rxjs-interop';
import { ActivatedRoute, RouterLink } from '@angular/router';
import { map } from 'rxjs';

//The site's catch-all route (data.notFound):  a url no route matches used to leave a blank page, with the router's
//NavigationError in the console as the only word (reviews/m3-closing.md #38).
@Component({
	selector: 'not-found',
	imports: [RouterLink],
	template: `<h1>Page not found</h1>
<p>There is no page at <code>{{url()}}</code>.</p>
<p><a routerLink="/">Go to the home page</a></p>`,
	host: {class: 'main-content my-content'}
})
export class NotFound{
	//the route's own segments, which under a top-level '**' are the whole path;  a signal, since a second unknown url reuses the page
	readonly url = toSignal( inject(ActivatedRoute).url.pipe(map( segments=>'/'+segments.map( s=>s.path ).join('/') )), {initialValue: '/'} );
}
