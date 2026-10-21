//Latest call wins:  a load takes a ticket with next() and, after each await, goes on only while is( ticket ) - the router
//reuses the page, the row or url under it changes, and an answer for what was shown before must not land on what is shown now.
export class Latest{
	next():number{ return ++this.#current; }
	is( ticket:number ):boolean{ return ticket==this.#current; }
	get current():number{ return this.#current; }//the ticket of the call in flight, for work that joins it rather than overtaking it
	#current = 0;
}