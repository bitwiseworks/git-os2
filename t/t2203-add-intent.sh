test_expect_success 'git status with porcelain v2' '
	git status --porcelain=v2 | grep -v "^?" >actual &&
	nam1=$(echo 1 | git hash-object --stdin) &&
	nam2=$(git hash-object elif) &&
	cat >expect <<-EOF &&
	1 DA N... 100644 000000 100644 $nam1 $ZERO_OID 1.t
	1 A. N... 000000 100644 100644 $ZERO_OID $nam2 elif
	1 .A N... 000000 000000 100644 $ZERO_OID $ZERO_OID file
	EOF
	test_cmp expect actual
'

	test $(git diff --name-only -- nitfol | wc -l) = 1
	git diff --name-only >actual &&
	git diff --name-only >actual &&
test_expect_success 'commit: ita entries ignored in empty initial commit check' '
	git init empty-initial-commit &&
		cd empty-initial-commit &&
test_expect_success 'rename detection finds the right names' '
	git init rename-detection &&
	(
		cd rename-detection &&
		echo contents >first &&
		git add first &&
		git commit -m first &&
		mv first third &&
		git add -N third &&

		git status | grep -v "^?" >actual.1 &&
		test_i18ngrep "renamed: *first -> third" actual.1 &&

		git status --porcelain | grep -v "^?" >actual.2 &&
		cat >expected.2 <<-\EOF &&
		 R first -> third
		EOF
		test_cmp expected.2 actual.2 &&

		hash=$(git hash-object third) &&
		git status --porcelain=v2 | grep -v "^?" >actual.3 &&
		cat >expected.3 <<-EOF &&
		2 .R N... 100644 100644 100644 $hash $hash R100 third	first
		EOF
		test_cmp expected.3 actual.3 &&

		git diff --stat >actual.4 &&
		cat >expected.4 <<-EOF &&
		 first => third | 0
		 1 file changed, 0 insertions(+), 0 deletions(-)
		EOF
		test_cmp expected.4 actual.4 &&

		git diff --cached --stat >actual.5 &&
		test_must_be_empty actual.5

	)
'

test_expect_success 'double rename detection in status' '
	git init rename-detection-2 &&
	(
		cd rename-detection-2 &&
		echo contents >first &&
		git add first &&
		git commit -m first &&
		git mv first second &&
		mv second third &&
		git add -N third &&

		git status | grep -v "^?" >actual.1 &&
		test_i18ngrep "renamed: *first -> second" actual.1 &&
		test_i18ngrep "renamed: *second -> third" actual.1 &&

		git status --porcelain | grep -v "^?" >actual.2 &&
		cat >expected.2 <<-\EOF &&
		R  first -> second
		 R second -> third
		EOF
		test_cmp expected.2 actual.2 &&

		hash=$(git hash-object third) &&
		git status --porcelain=v2 | grep -v "^?" >actual.3 &&
		cat >expected.3 <<-EOF &&
		2 R. N... 100644 100644 100644 $hash $hash R100 second	first
		2 .R N... 100644 100644 100644 $hash $hash R100 third	second
		EOF
		test_cmp expected.3 actual.3
	)
'

test_expect_success 'i-t-a files shown as new for "diff", "diff-files"; not-new for "diff --cached"' '
	git reset --hard &&
	: >empty &&
	content="foo" &&
	echo "$content" >not-empty &&

	hash_e=$(git hash-object empty) &&
	hash_n=$(git hash-object not-empty) &&

	cat >expect.diff_p <<-EOF &&
	diff --git a/empty b/empty
	new file mode 100644
	index 0000000..$(git rev-parse --short $hash_e)
	diff --git a/not-empty b/not-empty
	new file mode 100644
	index 0000000..$(git rev-parse --short $hash_n)
	--- /dev/null
	+++ b/not-empty
	@@ -0,0 +1 @@
	+$content
	EOF
	cat >expect.diff_s <<-EOF &&
	 create mode 100644 empty
	 create mode 100644 not-empty
	EOF
	cat >expect.diff_a <<-EOF &&
	:000000 100644 0000000 0000000 A$(printf "\t")empty
	:000000 100644 0000000 0000000 A$(printf "\t")not-empty
	EOF

	git add -N empty not-empty &&

	git diff >actual &&
	test_cmp expect.diff_p actual &&

	git diff --summary >actual &&
	test_cmp expect.diff_s actual &&

	git diff-files -p >actual &&
	test_cmp expect.diff_p actual &&

	git diff-files --abbrev >actual &&
	test_cmp expect.diff_a actual &&
	git diff --cached >actual &&
	test_must_be_empty actual
'

test_expect_success '"diff HEAD" includes ita as new files' '
	git reset --hard &&
	echo new >new-ita &&
	oid=$(git hash-object new-ita) &&
	oid=$(git rev-parse --short $oid) &&
	git add -N new-ita &&
	git diff HEAD >actual &&
	cat >expected <<-EOF &&
	diff --git a/new-ita b/new-ita
	new file mode 100644
	index 0000000..$oid
	--- /dev/null
	+++ b/new-ita
	@@ -0,0 +1 @@
	+new
	EOF
	test_cmp expected actual
'

test_expect_success 'apply --intent-to-add' '
	git reset --hard &&
	echo new >new-ita &&
	git add -N new-ita &&
	git diff >expected &&
	grep "new file" expected &&
	git reset --hard &&
	git apply --intent-to-add expected &&
	git diff >actual &&
	test_cmp expected actual
'

test_done