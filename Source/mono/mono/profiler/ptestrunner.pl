#!/usr/bin/perl -w

use strict;

# run the log profiler test suite

my $builddir = shift || die "Usage: ptestrunner.pl mono_build_dir\n";
my @errors = ();
my $total_errors = 0; # this is reset before each test
my $global_errors = 0;
my $report;
my $nunit_testcase_name;
my $nunit_testcase_xml;

my $profbuilddir = $builddir . "/mono/profiler";
my $minibuilddir = $builddir . "/mono/mini";

# Setup the execution environment
# for the profiler module
append_path ("DYLD_LIBRARY_PATH", $minibuilddir . "/.libs");
# for mprof-report
append_path ("PATH", $profbuilddir);

# first a basic test
$report = run_test ("test-alloc.exe", "report,legacy,calls,alloc");
check_report_basics ($report);
check_report_calls ($report, "T:Main (string[])" => 1);
check_report_allocation ($report, "System.Object" => 1000000);
report_errors ();
add_nunit_testcase_result ();
# test additional named threads and method calls
$report = run_test ("test-busy.exe", "report,legacy,calls,alloc");
check_report_basics ($report);
check_report_calls ($report, "T:Main (string[])" => 1);
check_report_threads ($report, "BusyHelper");
check_report_calls ($report, "T:test ()" => 10, "T:test3 ()" => 10, "T:test2 ()" => 1);
report_errors ();
add_nunit_testcase_result ();
# test with the sampling profiler
$report = run_test ("test-busy.exe", "report,legacy,sample");
check_report_basics ($report);
check_report_threads ($report, "BusyHelper");
# at least 40% of the samples should hit each of the two busy methods
# This seems to fail on osx, where the main thread gets the majority of SIGPROF signals
#check_report_samples ($report, "T:test ()" => 40, "T:test3 ()" => 40);
report_errors ();
add_nunit_testcase_result ();
# test lock events
$report = run_test ("test-monitor.exe", "report,legacy,calls,alloc");
check_report_basics ($report);
check_report_calls ($report, "T:Main (string[])" => 1);
# we hope for at least some contention, this is not entirely reliable
check_report_locks ($report, 1, 1);
report_errors ();
add_nunit_testcase_result ();
# test exceptions
$report = run_test ("test-excleave.exe", "report,legacy,calls");
check_report_basics ($report);
check_report_calls ($report, "T:Main (string[])" => 1, "T:throw_ex ()" => 1000);
check_report_exceptions ($report, 1000, 1000, 1000);
report_errors ();
add_nunit_testcase_result ();
# test heapshot
$report = run_test_sgen ("test-heapshot.exe", "report,heapshot,legacy");
if ($report ne "missing binary") {
	check_report_basics ($report);
	check_report_heapshot ($report, 0, {"T" => 5000});
	check_report_heapshot ($report, 1, {"T" => 5023});
	report_errors ();
	add_nunit_testcase_result ();
}
# test heapshot traces
$report = run_test_sgen ("test-heapshot.exe", "heapshot,output=traces.mlpd,legacy", "--traces traces.mlpd");
if ($report ne "missing binary") {
	check_report_basics ($report);
	check_report_heapshot ($report, 0, {"T" => 5000});
	check_report_heapshot ($report, 1, {"T" => 5023});
	check_heapshot_traces ($report, 0,
		T => [4999, "T"]
	);
	check_heapshot_traces ($report, 1,
		T => [5022, "T"]
	);
	report_errors ();
	add_nunit_testcase_result ();
}
# test traces
$report = run_test ("test-traces.exe", "legacy,calls,alloc,output=traces.mlpd", "--maxframes=7 --traces traces.mlpd");
check_report_basics ($report);
check_call_traces ($report,
	"T:level3 (int)" => [2020, "T:Main (string[])"],
	"T:level2 (int)" => [2020, "T:Main (string[])", "T:level3 (int)"],
	"T:level1 (int)" => [2020, "T:Main (string[])", "T:level3 (int)", "T:level2 (int)"],
	"T:level0 (int)" => [2020, "T:Main (string[])", "T:level3 (int)", "T:level2 (int)", "T:level1 (int)"]
);
check_exception_traces ($report,
	[1010, "T:Main (string[])", "T:level3 (int)", "T:level2 (int)", "T:level1 (int)", "T:level0 (int)"]
);
check_alloc_traces ($report,
	T => [1010, "T:Main (string[])", "T:level3 (int)", "T:level2 (int)", "T:level1 (int)", "T:level0 (int)"]
);
report_errors ();
add_nunit_testcase_result ();
# test traces without enter/leave events
$report = run_test ("test-traces.exe", "legacy,alloc,output=traces.mlpd", "--traces traces.mlpd");
check_report_basics ($report);
# this has been broken recently
check_exception_traces ($report,
	[1010, "T:Main (string[])", "T:level3 (int)", "T:level2 (int)", "T:level1 (int)", "T:level0 (int)"]
);
check_alloc_traces ($report,
	T => [1010, "T:Main (string[])", "T:level3 (int)", "T:level2 (int)", "T:level1 (int)", "T:level0 (int)"]
);
report_errors ();
add_nunit_testcase_result ();

emit_nunit_report();

exit ($global_errors ? 1 : 0);

# utility functions
sub append_path {
	my $var = shift;
	my $value = shift;
	if (exists $ENV{$var}) {
		$ENV{$var} = $value . ":" . $ENV{$var};
	} else {
		$ENV{$var} = $value;
	}
}

sub run_test
{
	return run_test_bin ("$minibuilddir/mono", @_);
}

sub run_test_sgen
{
	return run_test_bin ("$minibuilddir/mono-sgen", @_);
}

sub run_test_bin
{
	my $bin = shift;
	my $test_name = shift;
	my $option = shift || "report";
	my $roptions = shift;
	#clear the errors
	@errors = ();
	$total_errors = 0;
	print "Checking $test_name with $option ...";
	$nunit_testcase_name = "$test_name($option)";
	unless (-x $bin) {
		print "missing $bin, skipped.\n";
		return "missing binary";
	}
	my $report = `$bin --profile=log:$option $test_name`;
	print "\n";
	if (defined $roptions) {
		return `$profbuilddir/mprof-report $roptions`;
	}
	return $report;
}

sub report_errors
{
	foreach my $e (@errors) {
		print "Error: $e\n";
		$total_errors++;
		$global_errors++;
	}
	print "Total errors: $total_errors\n" if $total_errors;
	#print $report;
}

sub add_nunit_testcase_result
{
	my $successbool;
	if ($total_errors > 0) {
		$successbool = "False";
	} else {
		$successbool = "True";
	}

	$nunit_testcase_xml .= "              <test-case name=\"MonoTests.profiler.$nunit_testcase_name\" executed=\"True\" success=\"$successbool\" time=\"0\" asserts=\"0\"";
	if ($total_errors > 0) {
		$nunit_testcase_xml .=  ">\n";
		$nunit_testcase_xml .=  "                <failure>\n";
		$nunit_testcase_xml .=  "                  <message><![CDATA[";
		foreach my $e (@errors) {
			$nunit_testcase_xml .= "Error: $e\n";
		}
		$nunit_testcase_xml .= "]]></message>\n";
		$nunit_testcase_xml .=  "                  <stack-trace><![CDATA[";
		$nunit_testcase_xml .=  $report;
		$nunit_testcase_xml .=  "]]></stack-trace>\n";
		$nunit_testcase_xml .=  "                </failure>\n";
		$nunit_testcase_xml .=  "              </test-case>\n";
	} else {
		$nunit_testcase_xml .= " />\n";
	}
}

sub emit_nunit_report
{
	use Cwd;
	use POSIX qw(strftime uname locale_h);
	use Net::Domain qw(hostname hostfqdn);
	use locale;

	my $failed = $global_errors ? 1 : 0;
	my $successbool;
	my $total = 1;
	my $mylocale = setlocale (LC_CTYPE);
	$mylocale = substr($mylocale, 0, index($mylocale, '.'));
	$mylocale =~ s/_/-/;

	if ($failed > 0) {
		$successbool = "False";
	} else {
		$successbool = "True";
	}
	open (my $nunitxml, '>', 'TestResult-profiler.xml') or die "Could not write to 'TestResult-profiler.xml' $!";
	print $nunitxml "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"no\"?>\n";
	print $nunitxml "<!--This file represents the results of running a test suite-->\n";
	print $nunitxml "<test-results name=\"profiler-tests.dummy\" total=\"$total\" failures=\"$failed\" not-run=\"0\" date=\"" . strftime ("%F", localtime) . "\" time=\"" . strftime ("%T", localtime) . "\">\n";
	print $nunitxml "  <environment nunit-version=\"2.4.8.0\" clr-version=\"4.0.30319.17020\" os-version=\"Unix " . (uname ())[2]  . "\" platform=\"Unix\" cwd=\"" . getcwd . "\" machine-name=\"" . hostname . "\" user=\"" . getpwuid ($<) . "\" user-domain=\"" . hostfqdn  . "\" />\n";
	print $nunitxml "  <culture-info current-culture=\"$mylocale\" current-uiculture=\"$mylocale\" />\n";
	print $nunitxml "  <test-suite name=\"profiler-tests.dummy\" success=\"$successbool\" time=\"0\" asserts=\"0\">\n";
	print $nunitxml "    <results>\n";
	print $nunitxml "      <test-suite name=\"MonoTests\" success=\"$successbool\" time=\"0\" asserts=\"0\">\n";
	print $nunitxml "        <results>\n";
	print $nunitxml "          <test-suite name=\"profiler\" success=\"$successbool\" time=\"0\" asserts=\"0\">\n";
	print $nunitxml "            <results>\n";
	print $nunitxml $nunit_testcase_xml;
	print $nunitxml "            </results>\n";
	print $nunitxml "          </test-suite>\n";
	print $nunitxml "        </results>\n";
	print $nunitxml "      </test-suite>\n";
	print $nunitxml "    </results>\n";
	print $nunitxml "  </test-suite>\n";
	print $nunitxml "</test-results>\n";
	close $nunitxml;
}

sub get_delim_data
{
	my $report = shift;
	my $start = shift;
	my $end = shift;
	my $section = "";
	my $insection = 0;
	foreach (split (/\n/, $report)) {
		if ($insection) {
			#print "matching end $end vs $_\n";
			last if /$end/;
			$section .= $_;
			$section .= "\n";
		} else {
			#print "matching $start vs $_\n";
			$insection = 1 if (/$start/);
		}
	}
	return $section;
}

sub get_section
{
	my $report = shift;
	my $name = shift;
	return get_delim_data ($report, "^\Q$name\E", "^\\w.*summary");
}

sub get_heap_shot
{
	my $section = shift;
	my $num = shift;
	return get_delim_data ($report, "Heap shot $num at", "^\$");
}

sub check_report_basics
{
	my $report = shift;
	check_report_threads ($report, "Finalizer", "Main");
	check_report_metadata ($report, 2);
	check_report_jit ($report);
}

sub check_report_metadata
{
	my $report = shift;
	my $num = shift;
	my $section = get_section ($report, "Metadata");
	push @errors, "Wrong loaded images $num." unless $section =~ /Loaded images:\s$num/s;
}

sub check_report_calls
{
	my $report = shift;
	my %calls = @_;
	my $section = get_section ($report, "Method");
	foreach my $method (keys %calls) {
		push @errors, "Wrong calls to $method." unless $section =~ /\d+\s+\d+\s+($calls{$method})\s+\Q$method\E/s;
	}
}

sub check_call_traces
{
	my $report = shift;
	my %calls = @_;
	my $section = get_section ($report, "Method");
	foreach my $method (keys %calls) {
		my @desc = @{$calls{$method}};
		my $num = shift @desc;
		my $trace = get_delim_data ($section, "\\s+\\d+\\s+\\d+\\s+\\d+\\s+\Q$method\E", "^(\\s*\\d+\\s+\\d)|(^Total calls)");
		if ($trace =~ s/^\s+(\d+)\s+calls from:$//m) {
			my $num_calls = $1;
			push @errors, "Wrong calls to $method." unless $num_calls == $num;
			my @frames = map {s/^\s+(.*)\s*$/$1/; $_} split (/\n/, $trace);
			while (@desc) {
				my $dm = pop @desc;
				my $fm = pop @frames;
				push @errors, "Wrong frame $fm to $method." unless $dm eq $fm;
			}
		} else {
			push @errors, "No num calls for $method.";
		}
	}
}

sub check_alloc_traces
{
	my $report = shift;
	my %types = @_;
	my $section = get_section ($report, "Allocation");
	foreach my $type (keys %types) {
		my @desc = @{$types{$type}};
		my $num = shift @desc;
		my $trace = get_delim_data ($section, "\\s+\\d+\\s+\\d+\\s+\\d+\\s+\Q$type\E", "^(\\s*\\d+\\s+\\d)|(^Total)");
		if ($trace =~ s/^\s+(\d+)\s+bytes from:$//m) {
			#my $num_calls = $1;
			#push @errors, "Wrong calls to $method." unless $num_calls == $num;
			my @frames = map {s/^\s+(.*)\s*$/$1/; $_} split (/\n/, $trace);
			while (@desc) {
				my $dm = pop @desc;
				my $fm = pop @frames;
				while ($fm =~ /wrapper/) {
					$fm = pop @frames;
				}
				push @errors, "Wrong frame $fm for alloc of $type (expected $dm)." unless $dm eq $fm;
			}
		} else {
			push @errors, "No alloc frames for $type.";
		}
	}
}

sub check_heapshot_traces
{
	my $report = shift;
	my $hshot = shift;
	my %types = @_;
	my $section = get_section ($report, "Heap");
	$section = get_heap_shot ($section, $hshot);
	foreach my $type (keys %types) {
		my @desc = @{$types{$type}};
		my $num = shift @desc;
		my $rtype = shift @desc;
		my $trace = get_delim_data ($section, "\\s+\\d+\\s+\\d+\\s+\\d+\\s+\Q$type\E", "^\\s*\\d+\\s+\\d");
		if ($trace =~ s/^\s+(\d+)\s+references from:\s+\Q$rtype\E$//m) {
			my $num_refs = $1;
			push @errors, "Wrong num refs to $type from $rtype." unless $num_refs == $num;
		} else {
			push @errors, "No refs to $type from $rtype.";
		}
	}
}

sub check_exception_traces
{
	my $report = shift;
	my @etraces = @_;
	my $section = get_section ($report, "Exception");
	foreach my $d (@etraces) {
		my @desc = @{$d};
		my $num = shift @desc;
		my $trace = get_delim_data ($section, "^\\s+$num\\s+throws from:\$", "^\\s+(\\d+|Executed)");
		if (length ($trace)) {
			my @frames = map {s/^\s+(.*)\s*$/$1/; $_} split (/\n/, $trace);
			while (@desc) {
				my $dm = pop @desc;
				my $fm = pop @frames;
				push @errors, "Wrong frame '$fm' in exceptions (should be '$dm')." unless $dm eq $fm;
			}
		} else {
			push @errors, "No exceptions or incorrect number.";
		}
	}
}

sub check_report_samples
{
	my $report = shift;
	my %calls = @_;
	my $section = get_section ($report, "Statistical");
	foreach my $method (keys %calls) {
		push @errors, "Wrong samples for $method." unless ($section =~ /\d+\s+(\d+\.\d+)\s+\Q$method\E/s && $1 >= $calls{$method});
	}
}

sub check_report_allocation
{
	my $report = shift;
	my %allocs = @_;
	my $section = get_section ($report, "Allocation");
	foreach my $type (keys %allocs) {
		if ($section =~ /\d+\s+(\d+)\s+\d+\s+\Q$type\E$/m) {
			push @errors, "Wrong allocs for type $type." unless $1 >= $allocs{$type};
		} else {
			push @errors, "No allocs for type $type.";
		}
	}
}

sub check_report_heapshot
{
	my $report = shift;
	my $hshot = shift;
	my $typemap = shift;
	my %allocs = %{$typemap};
	my $section = get_section ($report, "Heap");
	$section = get_heap_shot ($section, $hshot);
	foreach my $type (keys %allocs) {
		if ($section =~ /\d+\s+(\d+)\s+\d+\s+\Q$type\E(\s+\(bytes.*\))?$/m) {
			push @errors, "Wrong heapshot for type $type at $hshot ($1, $allocs{$type})." unless $1 >= $allocs{$type};
		} else {
			push @errors, "No heapshot for type $type at heapshot $hshot.";
		}
	}
}

sub check_report_jit
{
	my $report = shift;
	my $min_methods = shift || 1;
	my $min_code = shift || 16;
	my $section = get_section ($report, "JIT");
	push @errors, "Not enough compiled method." unless (($section =~ /Compiled methods:\s(\d+)/s) && ($1 >= $min_methods));
	push @errors, "Not enough compiled code." unless (($section =~ /Generated code size:\s(\d+)/s) && ($1 >= $min_code));
}

sub check_report_locks
{
	my $report = shift;
	my $contentions = shift;
	my $acquired = shift;
	my $section = get_section ($report, "Monitor");
	push @errors, "Not enough contentions." unless (($section =~ /Lock contentions:\s(\d+)/s) && ($1 >= $contentions));
	push @errors, "Not enough acquired locks." unless (($section =~ /Lock acquired:\s(\d+)/s) && ($1 >= $acquired));
}

sub check_report_exceptions
{
	my $report = shift;
	my $throws = shift;
	my $catches = shift;
	my $finallies = shift;
	my $section = get_section ($report, "Exception");
	push @errors, "Not enough throws." unless (($section =~ /Throws:\s(\d+)/s) && ($1 >= $throws));
	push @errors, "Not enough catches." unless (($section =~ /Executed catch clauses:\s(\d+)/s) && ($1 >= $catches));
	push @errors, "Not enough finallies." unless (($section =~ /Executed finally clauses:\s(\d+)/s) && ($1 >= $finallies));
}

sub check_report_threads
{
	my $report = shift;
	my @threads = @_;
	my $section = get_section ($report, "Thread");
	foreach my $tname (@threads) {
		push @errors, "Missing thread $tname." unless $section =~ /Thread:.*name:\s"\Q$tname\E"/s;
	}
}

