import { ActivatedRoute } from '@angular/router';

//the route summary names the page ("Available Gateways"); the title is the fallback, unless it is an unsubstituted parameter like ":gateway"
export function pageHeading( route:ActivatedRoute ):string{
	const summary = <string|undefined>route.snapshot.data["summary"];
	const title = <string|undefined>route.snapshot.title;
	return summary ?? (title?.startsWith(":") ? "" : title ?? "");
}