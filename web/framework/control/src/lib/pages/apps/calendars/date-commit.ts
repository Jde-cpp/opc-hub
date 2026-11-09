import { Directive, output } from '@angular/core';

//A date field that acts on whole dates (#397).  Chromium fires `change` on each segment typed, so a month's first digit, or a
//year's - 0002 - would land as the date.  What is typed waits for Enter or for the field to be left;  a pick from the popup, with
//no key to it, lands at once.
@Directive({
	selector: 'input[dateCommit]',
	host: {'(keydown)': 'onKey($event)', '(click)': 'onClick()', '(change)': 'onChange()', '(blur)': 'settle()'}
})
export class DateCommit{
	dateCommit = output<void>();
	#typing = false;
	#changed = false;

	onKey( e:KeyboardEvent ):void{
		if( e.key=='Enter' )
			this.settle();
		else if( (e.key.length==1 && e.key!=' ') || ['ArrowUp', 'ArrowDown', 'Backspace', 'Delete'].includes( e.key ) )//a space opens the popup
			this.#typing = true;
	}
	onClick():void{ this.#typing = false; }
	onChange():void{
		this.#changed = true;
		if( !this.#typing )
			this.settle();
	}
	settle():void{
		if( this.#changed )
			this.dateCommit.emit();
		this.#typing = this.#changed = false;
	}
}