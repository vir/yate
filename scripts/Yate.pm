# Yate.pm
# (Gateway interface module for Yate)
#
# This file is part of the YATE Project http://YATE.null.ro
#
# Yet Another Telephony Engine - a fully featured software PBX and IVR
# Copyright (C) 2004-2006 Null Team
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.

package Yate;

use strict;
use warnings;

# Executed before everything.
BEGIN {
	# Have at least perl 5.6.1 to use this module.
	use 5.006_001;
	use Data::Dumper;

	# Set version && disable buffering.
	our $VERSION = '0.2';
	$ |= 1;
}

# All messages syntax.
my %headers = (
	# keyword         # fields
	'Error in'     => ['keyword', 'original'],
	'%%>message'   => ['keyword', 'id', 'time', 'name', 'retvalue'],
	'%%<message'   => ['keyword', 'id', 'processed', 'name', 'retvalue'],
	'%%<install'   => ['keyword', 'priority', 'name', 'success'],
	'%%<uninstall' => ['keyword', 'priority', 'name', 'success'],
	'%%<watch'     => ['keyword', 'name', 'success'],
	'%%<unwatch'   => ['keyword', 'name', 'success'],
	'%%<setlocal'  => ['keyword', 'name', 'value', 'success'],
);

# Act as OOP module.
sub new($;@) {
	my $class = shift;
	$class = ref($class) || $class;

	# I don't see why we accept additional parameters...
	# It's not needed at all. Maybe only 'Debug'.
	my $self = {
		'Debug' => 0,
		@_,
	};

	bless($self, $class);

	# The place to install events handled internally by this module
	$self->install_handlers();

	return $self;
}

# Install a handler to a given action (name/event/keyword).
sub install($$$;$;$$) {
	my ($self, $name, $handler, $priority, $filter_name, $filter_value) = @_;

	# Print install message only once for each event.
	if (not exists($self->{'_handlers'}->{$name})) {
		# Default priority is 100.
		$priority = 100 if (!$priority);

		# Format is %%>install:prior:name[:filter-name:[filter-value]].
		my $query = sprintf('%%%%>install:%s:%s', $self->escape($priority, ':'), $self->escape($name, ':'));

		# filter-name and filter-value aren't necessery.
		if ($filter_name) {
			$filter_value = '' if (!$filter_value);
			$query .= sprintf(':%s:%s', $self->escape($filter_name, ':'), $self->escape($filter_value, ':'));
		}

		$self->print($query);
	}

	push(@{$self->{'_handlers'}->{$name}}, $handler);

	return 1;
}

# Uninstall a handler from a given action (name/event/keyword) or uninstall given action with all handlers.
sub uninstall($$;$) {
	my ($self, $name, $handler) = @_;

	# Check if event exists at all.
	if (not exists($self->{'_handlers'}->{$name})) {
		return 0;
	}

	# Handler is given - delete it.
	if ($handler) {
		# Because we keep handlers in array we must loop the array, find the handler position,
		# move it to position 1 and just shift the array with 1 element (XXX fixme please).
		for (my $i = 0; $i <= $#{$self->{'_handlers'}->{$name}}; $i++) {
			if (${$self->{'_handlers'}->{$name}}[$i] == $handler) {
				(${$self->{'_handlers'}->{$name}}[$i], ${$self->{'_handlers'}->{$name}}[0]) = (${$self->{'_handlers'}->{$name}}[0], ${$self->{'_handlers'}->{$name}}[$i]) if ($i != 0);

				shift(@{$self->{'_handlers'}->{$name}});

				last;
			}
		}

		# After that just let the below check do its work (check for !$handler || !elements.
	}

	if (!$handler || $#{$self->{'_handlers'}->{$name}} == -1) {
		delete($self->{'_handlers'}->{$name});

		$self->print(sprintf('%%%%>uninstall:%s', $self->escape($name, ':')));
	}

	return 1;
}

# Wait for messages on STDIN.
sub listen($) {
	my ($self) = @_;

	while (my $line = <STDIN>) {
		# Get rid of \n at the end.
		chomp($line);

		# Message received. Parse && dispatch.
		$self->parse_message($line);
		$self->dispatch();
	}
}

# Return value for 'message' handlers.
sub retval($$) {
	my ($self, $return_value) = @_;

	$self->header('return_value', $return_value);
}

sub retvalue($$) {
	my ($self, $return_value) = @_;

	$self->retval($return_value);
}

# Set/Return all params.
sub params($;$) {
	my ($self, $params) = @_;

	$self->{'params'} = $params if ($params);

	if (exists($self->{'params'})) {
		return $self->{'params'};
	} else {
		return undef;
	}
}

# Set/Return all headers.
sub headers($;$) {
	my ($self, $headers) = @_;

	$self->{'headers'} = $headers if ($headers);

	if (exists($self->{'headers'})) {
		return $self->{'headers'};
	} else {
		return undef;
	}
}

# Set/Return a param.
sub param($$;$) {
	my ($self, $key, $value) = @_;

	$self->{'params'}->{$key} = $value if ($value);

	if (exists($self->{'params'}->{$key})) {
		return $self->{'params'}->{$key};
	} else {
		return undef;
	}
}

# Set/Return a headers.
sub header($$;$) {
	my ($self, $key, $value) = @_;

	$self->{'headers'}->{$key} = $value if ($value);

	if (exists($self->{'headers'}->{$key})) {
		return $self->{'headers'}->{$key};
	} else {
		return undef;
	}
}

# Print message to Engine.
sub print($$) {
	my ($self, $message) = @_;

	$self->debug('Printing ' . $message) if ($self->{'Debug'} == 1);
	print STDOUT $message . "\n";
}

# Output a DEBUG message.
sub debug($$) {
	my ($self, $message) = @_;

	$self->output_error('DEBUG: ' . $message);
}

# Output an ERROR message.
sub error($$) {
	my ($self, $message) = @_;

	$self->output_error('ERROR: ' . $message);
}

# Output a message to STDERR.
sub output_error($$) {
	my ($self, $message) = @_;

	print STDERR $message . "\n";
}

# Opposite to escape(). Convert %% to % and %'upper_code' to ASCII.
sub unescape($$) {
	my ($self, $string) = @_;

	$string =~ s/%(.)/$1 eq '%' ? '%' : (ord($1) < 64 ? $self->error('Invalid character sequence passed to unscape.') : chr(ord($1) - 64))/egs;

	return $string;
}

# Convert % to %%, and every ASCII char with code < 32 to %'upper_code' as stated in yate.null.ro/docs/extmodule.html.
sub escape($$;$) {
	my ($self, $string, $special) = @_;

	$string =~ s/(.)/$1 eq '%' ? '%%' : (ord($1) < 32 || ($special && $1 eq $special) ? '%' . chr(ord($1) + 64) : $1)/egs;

	return $string;
}

# Dump itself to STDERR.
sub dump($) {
	my ($self) = @_;

	$self->debug(Dumper($self));
}

# Parses messages and splits it to parts.
sub parse_message($$) {
	my ($self, $line) = @_;

	my ($msg_headers, $msg_params);

	$self->{'_raw_string'} = $line;
	$self->debug('Got message: ' . $line) if ($self->{'Debug'} == 1);

	(my $header) = $line =~ /^(.*?):/;
	if ($header && exists($headers{$header})) {
		# Removes the prefix and puts it in header.
		$line =~ s/^(%%[><])//;
		$msg_headers->{'prefix'} = $1;

		# Split line according to %headers (unescape fields keys/values).
		my @split_line = split(/:/, $line, $#{$headers{$header}} + 2);
		if ($#split_line < $#{$headers{$header}}) {
			$self->error('Got invalid count of parameters for keyword ' . $header);

			return 0;
		}

		for (my $i = 0; $i < @{$headers{$header}}; $i++) {
			$msg_headers->{${$headers{$header}}[$i]} = $self->unescape($split_line[$i]);
		}

		# Now handle params (if any).
		if ($#split_line > $#{$headers{$header}}) {
			foreach (split(/:/, $split_line[$#split_line])) {
				my ($key, $value) = split(/=/, $_, 2);
				$msg_params->{$self->unescape($key)} = $self->unescape($value);
			}
		}
	} else {
		$self->error('Got invalid keyword header');

		return 0;
	}

	# Set headers and params in main object.
	$self->headers($msg_headers);
	$self->params($msg_params);

	return 1;
}

# Tries to call a handler for the given keyword.
sub dispatch($) {
	my ($self) = @_;

	# It's not a message, check for 'keyword' binded handlers.
	if ($self->header('keyword') ne 'message') {
		if (exists($self->{'_handlers'}->{$self->header('keyword')})) {
			foreach (@{$self->{'_handlers'}->{$self->header('keyword')}}) {
				$_->($self);
			}
		}

		return 0;
	}

	if (not exists($self->{'_handlers'}->{$self->header('name')})) {
		$self->error('No handler for event ' . $self->header('name')) if ($self->{'Debug'} == 1);

		return 0;
	}

	foreach (@{$self->{'_handlers'}->{$self->header('name')}}) {
		my $return = $_->($self);

		if (lc($return) eq 'true' || $return eq '1') {
			$self->return_message('true', $self->header('return_value'));

			return 1;
		}
	}

	$self->return_message('false', '');
	$self->error('Could not dispatch event ' . $self->header('name'));

	return 0;
}

# Send %%<message to Engine.
sub return_message($$$) {
	my ($self, $processed, $return_value) = @_;

	if ($self->header('keyword') ne 'message' && $self->header('prefix') ne '%%>') {
		$self->error('return_message() called, but no %%>message received before that!');

		return 0;
	}

	my $params = '';
	while (my ($key, $value) = each(%{$self->params()})) {
		$params .= ':' . $self->escape($key, ':') . '=' . $self->escape($value, ':');
	}

	$self->print(sprintf('%%%%<message:%s:%s:%s:%s%s',
		$self->escape($self->header('id'), ':'),
		$self->escape($processed, ':'),
		$self->escape($self->header('name'), ':'),
		$self->escape($return_value, ':'),
		$params));

	return 1;
}

# Install default handlers.
sub install_handlers($) {
	my ($self) = @_;

        push(@{$self->{'_handlers'}->{'install'}}, \&handle_install);
        push(@{$self->{'_handlers'}->{'uninstall'}}, \&handle_uninstall);
        push(@{$self->{'_handlers'}->{'Error in'}}, \&handle_error_in);
}

# Default handler for uninstall.
sub handle_uninstall($) {
	my ($self) = @_;

	if ($self->header('success') eq 'true') {
		$self->debug('Uninstalled handler for ' . $self->header('name') . ' at priority ' . $self->header('priority') . '.') if ($self->{'Debug'} == 1);
	} else {
		$self->error('Cannot uninstall handler for ' . $self->header('name') . '.');
	}
}

# Default handler for install.
sub handle_install($) {
	my ($self) = @_;

	if ($self->header('success') eq 'true') {
		$self->debug('Installed handler for ' . $self->header('name') . ' at priority ' . $self->header('priority') . '.') if ($self->{'Debug'} == 1);
	} else {
		$self->error('Cannot install handler for ' . $self->header('name') . '.');

		delete($self->{'_handlers'}->{$self->header('name')}) if (exists($self->{'_handlers'}->{$self->header('name')}));
	}
}

# Default handler for 'Error in:' Engine to Application message.
sub handle_error_in($) {
	my ($self) = @_;

	$self->error('Received error in message we sent: ' . $self->header('original'));
}

1;

__END__

=head1 NAME

Yate - Gateway interface module for YATE (Yet Another Telephone Engine)

=head1 SYNOPSIS

    use Yate;
    use strict;
    use warnings;

    sub call_route_handler($) {
        my $message = shift;

        $message->retval('sip/sip:' . $message->param('called') . '@11.11.11.11');

        return 'true';
    }

    my $message = new Yate();
    # call.route or any other event.
    $message->O5Binstall('call.route', \&call_route_handler);
    $message->listen();

=head1 DESCRIPTION

This module provides interface for using Perl scripts as plugins in
Yate. With this developers can easily write a script that interacts
with Yate Engine. The scripts can track certain events (like
call.cdr), and return values which will change the state of the call
(i.e. with call.route you can set the route of the call).

=head1 METHODS

=head2 new

    my $message = new Yate(Debug => 1)

Creates an Yate object. Attributes are:

    Debug		Print verbose info.

By creating such an object you automatically create default handlers
for 'install', 'uninstall' and 'Error in' messages.

=head2 install

    $message->install($event, \&handler, $priority, filter_name, $filter_value)

Attaches a handler to the given event. All events can be found at
http://yate.null.ro/pmwiki/index.php?n=Main.StandardMessages. In
addition to those you can use install, uninstall, watch, unwatch
and setlocal. Those are internal messages sent between Engine and
Application, so it is recommended not to use them.

The priority is optional and defaults to 100. It can be any value
and it forces Yate to either first process another module (core
module like regexroute for example) or process first this script
and then the other modules (if any).

Filter name and filter value are optional. Filter name is the
name of the variable the handler will filter and filter value
is its value.

Whenever an event you given accures your subroutine will be called.
Exception is made only when another handler from your program
returns 'true' before the other handler is being processed.

The program starts listening for events after you call C<listen>.

Your events attached to a message should either return 'true' or
'false' (message processed or message not processed). If you
want to return a value to the Enigne use C<retval>. Only if you
return 'true' your return value will be passed to the Engine.
A message will be send to the Engine with all the parameters
and of course the return value.

Handlers will have only one argument and that is the current
Yate object itself. You can access headers and parameters by
C<header> and C<param>. For debugging the current structure
of the object use C<dump>.

If your handler returns 

=head2 uninstall

    $message->uninstall($event, $handler)

If you don't specify a handler, then all handlers are going to
be deattached from the event. If you do specify one only this
event will be deattached.

=head2 listen

    $message->listen()

Blocks the execution of the script from this point on and starts
to listen for events from the Engine.

=head2 retval, retvalue

    $message->retval($return_value)

This method should be used only from handlers attached to a message
event. It will return $return_value back to the Engine. It plays
a different role for every event. For more information visit
http://yate.null.ro/pmwiki/index.php?n=Main.StandardMessages.

=head2 params

    $message->params($params)

If no arguments are supplied it returns the current parameters, else
it sets the parameters to the given ones.

=head2 headers

    $message->headers($params)

If no arguments are supplied it returns the current headers, else
it sets the headers to the given ones.

=head2 param

    $message->param($key, $value)

If value is not specified returns the parameter matched by key or
undef if such parameter is not found. Else it sets the parameter
to the given value.

=head2 header

    $message->header($key, $value)

If value is not specified returns the header matched by key or
undef if such header is not found. Else it sets the header
to the given value.

=head2 print

    $message->print($message)

Sends a message directly to the engine. Use this method only
if you know what you are doing. If you send a wrong information
the execution of this script might not behave correctly or
you can even block the script. Don't use a trailing \n.

=head2 debug

    $message->debug($message)

Sends a debug message to the standart error stream. This
message will not be logged by the Engine or the Application
itself. This is only for debugging and should be used in
development stage. Don't use a trailing \n.

=head2 error

    $message->error($message)

Sends an error message to the standart error stream. Don't
use a trailing \n.

=head2 dump

    $message->dump()

Dumps all the information about the current Yate object.

=head2 escape

    $message->escape($string, $special_char)

Escapes the currrent string as stated in
http://yate.null.ro/docs/extmodule.html. Converts % into %% and
every character with ASCII code lower then 32 is converted to
%character('code + 32'). If you specify a special_char this
character will also be converted to the above code. This is only
needed if you pass arguments to the Engine. Special character
is ':' in most of the cases. Return value is escaped automatically,
the same goes for the parameters and headers you set.

=head1 INTERNAL METHODS

=head2 unescape

    $message->unescape($string)

Unescapes the current string. Every string passed to your handler
will be unescaped automatically.

=head2 output_error

    $message->output_error($message)

Sends a message to the standart error stream. This message is not
logged by anyone. Only for debugging propose.

=head2 parse_message

    $message->parse_message($line)

Parses a line received from the Engine and puts the headers and
parameters into the Yate object.

=head2 dispatch

    $message->dispatch()

Tries to call a handler for the given event (uses information
from the Yate object).

=head2 return_message

    $message->return_message($processed, $return_value)

This function is called automatically by C<dispatch> if the handler
return true. Processed is either true or false. return_value is the
value you set from C<retval>. It sends a message to the Engine
informing it if the event is handled or not.

=head2 install_handlers

    $message->install_handlers()

Installs default handlers. Called automaticall on object creation.
Default handlers are C<handle_uninstall>, C<handle_install> and
C<handle_error_in>.

=head2 handle_install

    $message->handle_install()

Default handler for 'install' messages received from Engine. This way
we can be sure that our handler for a given event has either been added
or not.

=head2 handle_uninstall

    $message->handle_uninstall()

Default handler for 'uninstall' messages received from Engine. This way
we can be sure that the removing of a given handler has either succeeded
or not.

=head2 handle_error_in

    $message->handle_error_in()

Default handler for 'Error in' messages received from Engine. Those are
messages that indicate fatal errors in a message we sent. They are logged
on the standart error stream.

=head1 EXAMPLES

Yet to come.

=head1 TODO

- Default handlers for watch and unwatch.

- Make functions for calling watch, unwatch, E<gt>message, output,
  setlocal and connect.

- Write examples in the documentation.

=head1 PROBLEMS

- We sent only 1 install message for each handler and the priority
  is taken only from the first C<install>.

- Instead of returning 'true' or 'false' we could return the 'real
  return value' and get rid off the function C<retval>. Returning
  'false' or 0 will then just not process the message.

- Deleting logic for a handler is not at all good.

=head1 SEE ALSO

Yate official website - http://yate.null.ro

Yate Application <=> Engine messages - http://yate.null.ro/docs/extmodule.html

=head1 BUGS

Please be so kind as to report any bugs at http://yate.null.ro/mantis/main_page.php.

=head1 COPYRIGHT AND LICENSE

Copyright (C) 2004-2006 Null Team

This library is free software; you can redistribute it and/or modify
it under the terms of the General Public License (GPL).  For
more information, see http://www.fsf.org/licenses/gpl.txt

=cut
