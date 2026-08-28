// Access control reaches a static member the same way it reaches an ordinary
// one: it is a member, and being shared by the class does not make it public.
class Ledger {
    static int secret;
public:
    int open;
};
int Ledger::secret = 1;

int main() {
    return Ledger::secret;
}
